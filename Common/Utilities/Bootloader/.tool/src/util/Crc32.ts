// Table-driven CRC-32 (reflected polynomial 0xEDB88320, the reflected form of 0x04C11DB7).
// The incremental form takes the previous final value, matching the device's Crc::Calculate(..., crc).


function buildCrcTable(): Uint32Array {
    const table: Uint32Array = new Uint32Array(256);

    for (let index: number = 0; index < 256; index++) {
        let value: number = index;

        for (let bit: number = 0; bit < 8; bit++) {
            value = (value & 1) !== 0 ? (0xEDB88320 ^ (value >>> 1)) : (value >>> 1);
        }

        table[index] = value >>> 0;
    }

    return table;
}


const crcTable: Uint32Array = buildCrcTable();


export function crc32(data: Uint8Array, previous?: number): number {
    let crc: number = previous === undefined ? 0xFFFFFFFF : (previous ^ 0xFFFFFFFF) >>> 0;

    for (const byte of data) {
        crc = (crc >>> 8) ^ (crcTable[(crc ^ byte) & 0xFF] as number);
    }

    return (crc ^ 0xFFFFFFFF) >>> 0;
}
