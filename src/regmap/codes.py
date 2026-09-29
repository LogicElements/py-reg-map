"""Fixed register-map vocabulary: data types, access types and register ID composition."""

from enum import StrEnum

BLOCK_COUNT = 26
MAX_BLOCK_CODE = BLOCK_COUNT - 1
MAX_ADDRESS = 0xFFF
MAX_MODBUS_ADDRESS = 0xFFFF


class RegType(StrEnum):
    BIN = "BIN"
    INT = "INT"
    FLOAT = "FLOAT"
    STRING = "STRING"
    ENUM = "ENUM"


class Access(StrEnum):
    RO = "RO"
    ROF = "ROF"
    RW = "RW"
    RWF = "RWF"
    RWIF = "RWIF"


TYPE_CODE = {
    RegType.BIN: 0,
    RegType.INT: 1,
    RegType.FLOAT: 2,
    RegType.STRING: 3,
    RegType.ENUM: 5,
}

ACCESS_CODE = {
    Access.RO: 0,
    Access.ROF: 1,
    Access.RW: 2,
    Access.RWF: 3,
    Access.RWIF: 3,
}

# None means "any size >= 1"
ALLOWED_SIZES: dict[RegType, tuple[int, ...] | None] = {
    RegType.BIN: (1, 2, 4, 8),
    RegType.INT: (1, 2, 4, 8),
    RegType.FLOAT: (4,),
    RegType.ENUM: (1, 2, 4),
    RegType.STRING: None,
}

FLASH_ACCESS = frozenset({Access.RWF, Access.ROF})
INPUT_ACCESS = frozenset({Access.RO, Access.ROF})


def register_id(block_code: int, address: int, reg_type: RegType, access: Access, size: int) -> int:
    """Compose the 32-bit register ID 0xBB_AAA_T_A_L."""
    log2_size = size.bit_length() - 1  # floor(log2(size)) for size >= 1
    return (
        (block_code << 24)
        | (address << 12)
        | (TYPE_CODE[reg_type] << 8)
        | ((1 + 2 * ACCESS_CODE[access]) << 4)
        | log2_size
    )
