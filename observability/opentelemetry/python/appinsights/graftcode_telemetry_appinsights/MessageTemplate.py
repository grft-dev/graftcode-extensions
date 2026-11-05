_NULL_VALUE = "(null)"


class MessageTemplate:

    @staticmethod
    def render(template, args, properties):
        if template is None:
            return ""

        values = args or ()
        builder = []
        arg_index = 0
        length = len(template)
        i = 0

        while i < length:
            character = template[i]

            if character in "{}" and i + 1 < length and template[i + 1] == character:
                builder.append(character)
                i += 2
                continue

            closing_index = template.find("}", i + 1) if character == "{" else -1
            if closing_index < 0 or arg_index >= len(values):
                builder.append(character)
                i += 1
                continue

            value = MessageTemplate._format(values[arg_index])
            arg_index += 1
            builder.append(value)

            name = MessageTemplate._property_name(template[i + 1:closing_index])
            if name:
                properties[name] = value

            i = closing_index + 1

        return "".join(builder)

    @staticmethod
    def _property_name(placeholder):
        end = len(placeholder)
        for index, character in enumerate(placeholder):
            if character in ",:":
                end = index
                break

        name = placeholder[:end].strip()
        if name.startswith("@") or name.startswith("$"):
            name = name[1:]

        return name

    @staticmethod
    def _format(value):
        if value is None:
            return _NULL_VALUE

        try:
            return str(value)
        except Exception:
            value_type = type(value)
            return value_type.__module__ + "." + value_type.__qualname__
