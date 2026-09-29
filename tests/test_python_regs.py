from helpers import resolved

from regmap.generators import python_regs


def test_python_class_with_enums():
    rmap = resolved(
        """
        SYS:
          code: 0
          registers:
            - {name: UPTIME, type: INT, access: RO, size: 4}
            - {name: MODE, type: ENUM, access: RW, size: 1, values: [{name: M_A}, {name: M_B, value: 3}]}
        """
    )
    assert python_regs.render(rmap) == (
        "class DevRegs:\n"
        '    SYS_UPTIME = "SYS_UPTIME"\n'
        '    SYS_MODE = "SYS_MODE"\n'
        "\n\n"
        "class SYS_MODE:\n"
        "    M_A = 0\n"
        "    M_B = 3\n"
        "\n\n"
    )


def test_map_without_registers_is_still_valid_python():
    text = python_regs.render(resolved("SYS: {code: 0, registers: []}"))
    assert text == "class DevRegs:\n    pass\n\n\n"
    compile(text, "DevRegs.py", "exec")
