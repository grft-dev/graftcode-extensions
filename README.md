# graftcode-extensions

Official open-source Graftcode Gateway plugins for carrying Graft calls over external communication channels instead of the Gateway's built-in servers. Each plugin implements the same gateway and transport interfaces and is selected purely by configuration, via the Gateway's `--config` option.

## Plugins

| Plugin | Channel |
|--------|---------|
| [rabbitmq](rabbitmq/) | RabbitMQ (AMQP 0-9-1), request/reply |
| [servicebus](servicebus/) | Azure Service Bus (AMQP 1.0), request/reply and one-way |
| [observability/opentelemetry](observability/opentelemetry/) | OpenTelemetry / Azure Application Insights connector |

Each plugin has its own README with build and configuration steps. For how the Gateway loads a plugin, see the "Plugin server config" section of the [Graftcode Gateway](https://github.com/grft-dev/graftcode-gateway) README.

Part of the [Graftcode](https://github.com/grft-dev/graftcode) project.
