package com.graftcode.instrumentation;

public final class GraftcodeTelemetrySettings {
    static final int MINIMUM_BODY_MAX_CHARS = 256;
    static final int DEFAULT_BODY_MAX_CHARS = 8192;
    static final int MAXIMUM_BODY_MAX_CHARS = 32768;

    private static volatile boolean autoInject = readFlag("HYPERTUBE_TELEMETRY_AUTO_INJECT", true);
    private static volatile boolean captureBodies = readFlag("HYPERTUBE_TELEMETRY_CAPTURE_BODIES", false);
    private static volatile int bodyMaxCharacters = readBodyMaxChars();

    private GraftcodeTelemetrySettings() {
    }

    public static boolean isAutoInjectEnabled() {
        return autoInject;
    }

    public static void setAutoInjectEnabled(boolean value) {
        autoInject = value;
    }

    public static boolean isBodyCaptureEnabled() {
        return captureBodies;
    }

    public static void setBodyCaptureEnabled(boolean value) {
        captureBodies = value;
    }

    public static int getBodyMaxCharacters() {
        return bodyMaxCharacters;
    }

    public static void setBodyMaxCharacters(int value) {
        bodyMaxCharacters = clampBodyMaxCharacters(value);
    }

    static int clampBodyMaxCharacters(int value) {
        if (value < MINIMUM_BODY_MAX_CHARS) {
            return MINIMUM_BODY_MAX_CHARS;
        }

        return Math.min(value, MAXIMUM_BODY_MAX_CHARS);
    }

    private static int readBodyMaxChars() {
        String raw = System.getenv("HYPERTUBE_TELEMETRY_BODY_MAX_CHARS");
        if (raw == null || raw.trim().isEmpty()) {
            return DEFAULT_BODY_MAX_CHARS;
        }

        try {
            return clampBodyMaxCharacters(Integer.parseInt(raw.trim()));
        } catch (NumberFormatException ex) {
            return DEFAULT_BODY_MAX_CHARS;
        }
    }

    private static boolean readFlag(String name, boolean defaultValue) {
        String value = System.getenv(name);
        if (value == null || value.trim().isEmpty()) {
            return defaultValue;
        }

        return !"false".equalsIgnoreCase(value.trim()) && !"0".equals(value.trim());
    }
}
