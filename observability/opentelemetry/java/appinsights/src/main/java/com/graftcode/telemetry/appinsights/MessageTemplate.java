package com.graftcode.telemetry.appinsights;

import java.util.Map;

final class MessageTemplate {
    private static final String NULL_VALUE = "(null)";

    private MessageTemplate() {
    }

    static String render(String template, Object[] args, Map<String, String> properties) {
        if (template == null) {
            return "";
        }

        Object[] values = args == null ? new Object[0] : args;
        StringBuilder builder = new StringBuilder(template.length() + 32);
        int argIndex = 0;

        for (int i = 0; i < template.length(); i++) {
            char character = template.charAt(i);

            if ((character == '{' || character == '}')
                    && i + 1 < template.length()
                    && template.charAt(i + 1) == character) {
                builder.append(character);
                i++;
                continue;
            }

            int closingIndex = character == '{' ? template.indexOf('}', i + 1) : -1;
            if (closingIndex < 0 || argIndex >= values.length) {
                builder.append(character);
                continue;
            }

            String value = format(values[argIndex++]);
            builder.append(value);

            String name = propertyName(template.substring(i + 1, closingIndex));
            if (!name.isEmpty()) {
                properties.put(name, value);
            }

            i = closingIndex;
        }

        return builder.toString();
    }

    private static String propertyName(String placeholder) {
        int end = placeholder.length();
        for (int i = 0; i < placeholder.length(); i++) {
            char character = placeholder.charAt(i);
            if (character == ',' || character == ':') {
                end = i;
                break;
            }
        }

        String name = placeholder.substring(0, end).trim();
        if (name.startsWith("@") || name.startsWith("$")) {
            name = name.substring(1);
        }

        return name;
    }

    private static String format(Object value) {
        if (value == null) {
            return NULL_VALUE;
        }

        try {
            return String.valueOf(value);
        } catch (RuntimeException ex) {
            return value.getClass().getName();
        }
    }
}
