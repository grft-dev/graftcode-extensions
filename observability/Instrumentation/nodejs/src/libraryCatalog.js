import { readFileSync, statSync } from 'node:fs'
import { basename, dirname, extname, join } from 'node:path'
import { CommandType, connectionTypeName, isCommand, runtimeNameOf } from 'hypertube-nodejs-sdk/abstractions'

const MAX_DEPTH = 32
const ENTRY_FILE_NAMES = new Set(['index', 'main'])

const librariesByConnection = new Map()
const versionByPath = new Map()

export function recordLibraries(command, connectionData) {
    for (const load of findCommands(command, CommandType.LoadLibrary, 0)) {
        const path = libraryPath(load)
        const name = path ? libraryNameFromPath(path) : undefined
        if (!path || !name) {
            continue
        }

        const key = connectionKey(connectionData)
        let libraries = librariesByConnection.get(key)
        if (!libraries) {
            libraries = new Map()
            librariesByConnection.set(key, libraries)
        }
        libraries.set(name.toLowerCase(), { name, version: readVersion(path) })
    }
}

export function describeTarget(command, connectionData, operation, configured) {
    const channel = channelName(connectionData)
    const library = resolveLibrary(command, connectionData, operation, configured)
    if (!library) {
        return `${runtimeNameOf(command.runtimeName)} ${channel}`
    }
    return library.version ? `${library.name} ${library.version} ${channel}` : `${library.name} ${channel}`
}

export function channelName(connectionData) {
    return typeof connectionData.connectionType === 'number'
        ? connectionTypeName(connectionData.connectionType).toLowerCase()
        : 'unknown'
}

export function resetLibraryCatalog() {
    librariesByConnection.clear()
    versionByPath.clear()
}

function resolveLibrary(command, connectionData, operation, configured) {
    const named = configuredName(configured, connectionData, command, operation)
    if (named) {
        return lookup(connectionData, named) ?? { name: named, version: '' }
    }

    for (const load of findCommands(command, CommandType.LoadLibrary, 0)) {
        const path = libraryPath(load)
        const name = path ? libraryNameFromPath(path) : undefined
        if (name) {
            return lookup(connectionData, name) ?? { name, version: '' }
        }
    }

    const libraries = librariesByConnection.get(connectionKey(connectionData))
    const typeName = operation.service
    if (!typeName) {
        return undefined
    }

    if (libraries) {
        let best
        for (const library of libraries.values()) {
            if (isTypeOfLibrary(typeName, library.name) && (!best || library.name.length > best.name.length)) {
                best = library
            }
        }
        if (best) {
            return best
        }
    }

    const separator = typeName.indexOf('.')
    if (separator > 0) {
        return { name: typeName.slice(0, separator), version: '' }
    }

    if (libraries?.size === 1) {
        return libraries.values().next().value
    }
    return undefined
}

function configuredName(configured, connectionData, command, operation) {
    if (!configured) {
        return undefined
    }

    const target = connectionData.hostname ?? ''
    let name
    try {
        name =
            typeof configured === 'function'
                ? configured({
                      target,
                      channel: channelName(connectionData),
                      runtime: runtimeNameOf(command.runtimeName),
                      service: operation.service,
                      method: operation.method,
                  })
                : (configured[target] ?? configured[target.replace(/\/+$/, '')])
    } catch {
        return undefined
    }
    return typeof name === 'string' && name.trim() ? name.trim() : undefined
}

function lookup(connectionData, name) {
    return librariesByConnection.get(connectionKey(connectionData))?.get(name.toLowerCase())
}

function isTypeOfLibrary(typeName, libraryName) {
    const type = typeName.toLowerCase()
    const library = libraryName.toLowerCase()
    return type === library || type.startsWith(`${library}.`)
}

function connectionKey(connectionData) {
    return `${connectionData.connectionType}|${connectionData.hostname ?? ''}`
}

function findCommands(command, commandType, depth) {
    if (depth > MAX_DEPTH || !isCommand(command)) {
        return []
    }

    const matches = command.commandType === commandType ? [command] : []
    for (const item of command.payload) {
        matches.push(...findCommands(item, commandType, depth + 1))
    }
    return matches
}

function libraryPath(load) {
    for (let i = load.payload.length - 1; i >= 0; i--) {
        const item = load.payload[i]
        const text = isCommand(item) && item.commandType === CommandType.Value ? item.payload[0] : item
        if (typeof text === 'string' && text.trim()) {
            return text.trim()
        }
    }
    return undefined
}

function libraryNameFromPath(path) {
    const trimmed = path.replace(/[\\/]+$/, '')
    const file = basename(trimmed)
    if (file.toLowerCase() === 'package.json') {
        return basename(dirname(trimmed)) || undefined
    }

    const name = file.slice(0, file.length - extname(file).length) || file
    if (ENTRY_FILE_NAMES.has(name.toLowerCase()) && /\.[cm]?js$/i.test(file)) {
        return basename(dirname(trimmed)) || name
    }
    return name || undefined
}

function readVersion(path) {
    const cached = versionByPath.get(path)
    if (cached !== undefined) {
        return cached
    }

    let version = ''
    try {
        const trimmed = path.replace(/[\\/]+$/, '')
        const packageJson = trimmed.toLowerCase().endsWith('package.json')
            ? trimmed
            : statSync(trimmed).isDirectory()
              ? join(trimmed, 'package.json')
              : join(dirname(trimmed), 'package.json')
        const parsed = JSON.parse(readFileSync(packageJson, 'utf8'))
        version = typeof parsed.version === 'string' ? parsed.version.trim() : ''
    } catch {
        version = ''
    }
    versionByPath.set(path, version)
    return version
}
