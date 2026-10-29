import assert from 'node:assert/strict'
import { describe, test } from 'node:test'
import { CommandType, RuntimeName } from 'hypertube-nodejs-sdk/observability'
import { resolveOperation } from '../src/index.js'

const command = (commandType, payload) => ({ runtimeName: RuntimeName.Nodejs, commandType, payload })
const getType = (name) => command(CommandType.GetType, [name])
const value = (inner) => command(CommandType.Value, [inner])

describe('resolveOperation', () => {
    test('names member calls Service/method', () => {
        assert.deepEqual(resolveOperation(command(CommandType.InvokeStaticMethod, [getType('Lib.RandomService'), 'fetch'])), {
            name: 'Lib.RandomService/fetch',
            service: 'Lib.RandomService',
            method: 'fetch',
            commandType: 'InvokeStaticMethod',
        })
    })

    test('resolves the type through nested commands and Value wrappers', () => {
        const created = command(CommandType.CreateClassInstance, [getType('Calculator')])
        assert.equal(resolveOperation(command(CommandType.InvokeInstanceMethod, [created, value('add')])).name, 'Calculator/add')
        assert.equal(resolveOperation(created).name, 'Calculator/constructor')
    })

    test('names global functions by the function alone and skips control commands', () => {
        assert.equal(resolveOperation(command(CommandType.InvokeGlobalFunction, ['parseInt'])).name, 'parseInt')
        assert.equal(resolveOperation(command(CommandType.LoadLibrary, ['lib.js'])), undefined)
        assert.equal(resolveOperation(getType('Math')), undefined)
        assert.equal(resolveOperation('not a command'), undefined)
    })

    test('calls on an existing instance reference keep the method name', () => {
        const reference = command(CommandType.Reference, ['5f1c'])
        assert.equal(resolveOperation(command(CommandType.InvokeInstanceMethod, [reference, 'next'])).name, 'next')
    })
})
