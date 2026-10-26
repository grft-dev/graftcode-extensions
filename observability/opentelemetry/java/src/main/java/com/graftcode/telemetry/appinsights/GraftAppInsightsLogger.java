package com.graftcode.telemetry.appinsights;

import com.hypertube.utils.logging.LogsLevel;
import io.opentelemetry.api.common.AttributeKey;
import io.opentelemetry.api.logs.LogRecordBuilder;
import io.opentelemetry.api.logs.Logger;
import io.opentelemetry.api.logs.Severity;
import io.opentelemetry.context.Context;

import java.io.PrintStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.LinkedHashMap;
import java.util.Map;

public final class GraftAppInsightsLogger {
    private static final String CONSOLE_INDENT = "      ";

    private final String category;
    private final Logger logger;
    private final LogsLevel minimumLevel;
    private final PrintStream console;

    GraftAppInsightsLogger(String category, Logger logger, LogsLevel minimumLevel, PrintStream console) {
        this.category = category;
        this.logger = logger;
        this.minimumLevel = minimumLevel;
        this.console = console;
    }

    public boolean isEnabled(LogsLevel level) {
        return level != LogsLevel.Off && minimumLevel != LogsLevel.Off && level.isAtLeast(minimumLevel);
    }

    public void trace(String template, Object... args) {
        log(LogsLevel.Trace, null, template, args);
    }

    public void debug(String template, Object... args) {
        log(LogsLevel.Debug, null, template, args);
    }

    public void info(String template, Object... args) {
        log(LogsLevel.Info, null, template, args);
    }

    public void warning(String template, Object... args) {
        log(LogsLevel.Warning, null, template, args);
    }

    public void error(String template, Object... args) {
        log(LogsLevel.Error, null, template, args);
    }

    public void error(Throwable exception, String template, Object... args) {
        log(LogsLevel.Error, exception, template, args);
    }

    public void log(LogsLevel level, String template, Object... args) {
        log(level, null, template, args);
    }

    public void log(LogsLevel level, Throwable exception, String template, Object... args) {
        if (!isEnabled(level)) {
            return;
        }

        Map<String, String> properties = new LinkedHashMap<>();
        String message = MessageTemplate.render(template, args, properties);

        emit(level, exception, template, message, properties);
        writeToConsole(level, exception, message);
    }

    private void emit(LogsLevel level, Throwable exception, String template, String message, Map<String, String> properties) {
        LogRecordBuilder record = logger.logRecordBuilder()
                .setContext(Context.current())
                .setSeverity(toSeverity(level))
                .setSeverityText(toLevelName(level))
                .setBody(message);

        for (Map.Entry<String, String> property : properties.entrySet()) {
            record.setAttribute(AttributeKey.stringKey(property.getKey()), property.getValue());
        }

        if (template != null) {
            record.setAttribute(AttributeKey.stringKey("{OriginalFormat}"), template);
        }

        if (exception != null) {
            record.setAttribute(AttributeKey.stringKey("exception.type"), exception.getClass().getName());
            if (exception.getMessage() != null) {
                record.setAttribute(AttributeKey.stringKey("exception.message"), exception.getMessage());
            }
            record.setAttribute(AttributeKey.stringKey("exception.stacktrace"), stackTrace(exception));
        }

        record.emit();
    }

    private void writeToConsole(LogsLevel level, Throwable exception, String message) {
        if (console == null) {
            return;
        }

        StringBuilder line = new StringBuilder()
                .append(toConsoleLevel(level)).append(": ").append(category).append("[0]")
                .append(System.lineSeparator())
                .append(CONSOLE_INDENT).append(message);

        if (exception != null) {
            line.append(System.lineSeparator()).append(stackTrace(exception).trim());
        }

        synchronized (console) {
            console.println(line);
        }
    }

    private static String stackTrace(Throwable exception) {
        StringWriter writer = new StringWriter();
        exception.printStackTrace(new PrintWriter(writer));
        return writer.toString();
    }

    private static Severity toSeverity(LogsLevel level) {
        switch (level) {
            case Trace:
                return Severity.TRACE;
            case Debug:
                return Severity.DEBUG;
            case Info:
                return Severity.INFO;
            case Warning:
                return Severity.WARN;
            case Error:
                return Severity.ERROR;
            default:
                return Severity.UNDEFINED_SEVERITY_NUMBER;
        }
    }

    private static String toLevelName(LogsLevel level) {
        return level == LogsLevel.Info ? "Information" : level.name();
    }

    private static String toConsoleLevel(LogsLevel level) {
        switch (level) {
            case Trace:
                return "trce";
            case Debug:
                return "dbug";
            case Info:
                return "info";
            case Warning:
                return "warn";
            default:
                return "fail";
        }
    }
}
