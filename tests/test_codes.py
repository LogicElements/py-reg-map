import pytest

from regmap.codes import Access, RegType, register_id


@pytest.mark.parametrize(
    ("code", "address", "reg_type", "access", "size", "expected"),
    [
        (0, 0, RegType.INT, Access.RO, 4, 0x00000112),  # SYS_UPTIME
        (0, 4, RegType.INT, Access.ROF, 4, 0x00004132),  # SYS_REGMAP_VERSION
        (0, 8, RegType.BIN, Access.RO, 4, 0x00008012),  # SYS_STATUS
        (0, 20, RegType.INT, Access.RO, 2, 0x00014111),  # SYS_IO_INPUT
        (3, 0, RegType.ENUM, Access.RWF, 1, 0x03000570),  # COM_MB_BAUD_RATE
        (3, 3, RegType.INT, Access.RW, 1, 0x03003150),  # COM_RESERVED
        (3, 8, RegType.INT, Access.RWF, 2, 0x03008171),  # COM_MB_TIMEOUT
        (6, 0, RegType.INT, Access.RO, 4, 0x06000112),  # DBG_WRITES_CONF
    ],
)
def test_register_id_matches_vms1511(code, address, reg_type, access, size, expected):
    assert register_id(code, address, reg_type, access, size) == expected


def test_rwif_has_the_same_access_code_as_rwf():
    assert register_id(4, 0, RegType.INT, Access.RWIF, 4) == register_id(
        4, 0, RegType.INT, Access.RWF, 4
    )


@pytest.mark.parametrize(
    ("size", "log2"), [(1, 0), (2, 1), (3, 1), (4, 2), (8, 3), (16, 4), (20, 4)]
)
def test_size_field_is_floor_log2(size, log2):
    assert register_id(0, 0, RegType.STRING, Access.RO, size) & 0xF == log2
