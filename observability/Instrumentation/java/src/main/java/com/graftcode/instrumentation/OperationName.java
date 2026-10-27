package com.graftcode.instrumentation;

import com.hypertube.utils.CommandType;
import com.hypertube.utils.ICommand;

final class OperationName {
    private static final int MAX_DEPTH = 32;

    private OperationName() {
    }

    static String resolve(ICommand command) {
        switch (command.getCommandType()) {
            case INVOKE_STATIC_METHOD:
            case INVOKE_INSTANCE_METHOD:
            case INVOKE_GENERIC_STATIC_METHOD:
            case INVOKE_GENERIC_METHOD:
            case GET_STATIC_FIELD:
            case SET_STATIC_FIELD:
            case GET_INSTANCE_FIELD:
            case SET_INSTANCE_FIELD:
            case GET_STATIC_METHOD_AS_DELEGATE:
            case GET_INSTANCE_METHOD_AS_DELEGATE: {
                String qualified = qualify(
                        resolveTypeNameCore(payloadAt(command, 0), 0),
                        resolveText(payloadAt(command, 1), 0));
                return qualified != null ? qualified : fallback(command);
            }

            case CREATE_INSTANCE: {
                String createdType = resolveTypeNameCore(payloadAt(command, 0), 0);
                return createdType == null ? fallback(command) : createdType + ".ctor";
            }

            case INVOKE_GLOBAL_FUNCTION:
            case GET_GLOBAL_FIELD: {
                String name = resolveText(payloadAt(command, 0), 0);
                return name != null ? name : fallback(command);
            }

            default:
                return fallback(command);
        }
    }

    static String resolveTargetTypeName(ICommand command) {
        switch (command.getCommandType()) {
            case INVOKE_STATIC_METHOD:
            case INVOKE_INSTANCE_METHOD:
            case INVOKE_GENERIC_STATIC_METHOD:
            case INVOKE_GENERIC_METHOD:
            case GET_STATIC_FIELD:
            case SET_STATIC_FIELD:
            case GET_INSTANCE_FIELD:
            case SET_INSTANCE_FIELD:
            case GET_STATIC_METHOD_AS_DELEGATE:
            case GET_INSTANCE_METHOD_AS_DELEGATE:
            case CREATE_INSTANCE:
                return resolveTypeNameCore(payloadAt(command, 0), 0);

            case GET_TYPE:
                return resolveTypeNameCore(command, 0);

            default:
                return null;
        }
    }

    private static String fallback(ICommand command) {
        return "hypertube." + command.getCommandType();
    }

    private static String qualify(String typeName, String memberName) {
        if (isBlank(memberName)) {
            return null;
        }

        return isBlank(typeName) ? memberName : typeName + "." + memberName;
    }

    private static Object payloadAt(ICommand command, int index) {
        Object[] payload = command.getPayload();
        return payload.length > index ? payload[index] : null;
    }

    private static String resolveText(Object payloadItem, int depth) {
        if (depth > MAX_DEPTH) {
            return null;
        }

        if (payloadItem instanceof String) {
            return (String) payloadItem;
        }

        if (payloadItem instanceof ICommand && ((ICommand) payloadItem).getCommandType() == CommandType.VALUE) {
            return resolveText(payloadAt((ICommand) payloadItem, 0), depth + 1);
        }

        return null;
    }

    private static String resolveTypeNameCore(Object target, int depth) {
        if (depth > MAX_DEPTH || target == null) {
            return null;
        }

        if (target instanceof String) {
            return (String) target;
        }

        if (target instanceof Class) {
            return ((Class<?>) target).getName();
        }

        if (target instanceof ICommand) {
            return resolveTypeNameFromCommand((ICommand) target, depth);
        }

        return target.getClass().getName();
    }

    private static String resolveTypeNameFromCommand(ICommand command, int depth) {
        switch (command.getCommandType()) {
            case VALUE:
                return resolveTypeNameCore(payloadAt(command, 0), depth + 1);

            case GET_TYPE:
            case CAST:
            case CREATE_NULL:
                for (Object item : command.getPayload()) {
                    String name = resolveText(item, depth + 1);
                    if (!isBlank(name)) {
                        return name;
                    }
                }

                return null;

            case CREATE_INSTANCE:
            case SET_INSTANCE_FIELD:
            case INVOKE_STATIC_METHOD:
            case INVOKE_INSTANCE_METHOD:
            case GET_STATIC_FIELD:
            case GET_INSTANCE_FIELD:
                return resolveTypeNameCore(payloadAt(command, 0), depth + 1);

            default:
                return null;
        }
    }

    static boolean isBlank(String value) {
        return value == null || value.trim().isEmpty();
    }
}
