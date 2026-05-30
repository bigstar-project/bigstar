param(
    [Parameter(Mandatory = $true)]
    [string]$MainRamDump,
    [string]$OutCsv = "",
    [int]$MinObjectId = 1,
    [int]$MaxObjectId = 0x3ff
)

$ErrorActionPreference = "Stop"

function Read-U16LE([byte[]]$bytes, [int]$offset) {
    return [BitConverter]::ToUInt16($bytes, $offset)
}

function Read-U32LE([byte[]]$bytes, [int]$offset) {
    return [BitConverter]::ToUInt32($bytes, $offset)
}

function Hex32([uint32]$value) {
    return ("0x{0:x8}" -f $value)
}

$bytes = [IO.File]::ReadAllBytes((Resolve-Path $MainRamDump))
$rows = New-Object System.Collections.Generic.List[object]

for ($off = 0; $off -le $bytes.Length - 0x120; $off += 4) {
    $vtable = Read-U32LE $bytes $off
    if ($vtable -lt 0x02000000 -or $vtable -ge (0x02000000 + $bytes.Length)) {
        continue
    }

    $guid = Read-U32LE $bytes ($off + 4)
    if ($guid -eq 0 -or $guid -ge 0x10000) {
        continue
    }

    $settings = Read-U32LE $bytes ($off + 8)
    $objectId = Read-U16LE $bytes ($off + 0x0c)
    $stateType = Read-U16LE $bytes ($off + 0x0e)
    $flags = Read-U32LE $bytes ($off + 0x10)
    if ($objectId -lt $MinObjectId -or $objectId -gt $MaxObjectId) {
        continue
    }
    if ($stateType -ne 1 -and $stateType -ne 2 -and $stateType -ne 3) {
        continue
    }
    if ($flags -ge 0x01000000) {
        continue
    }

    $rows.Add([pscustomobject]@{
        offset = Hex32 ([uint32]$off)
        base = Hex32 ([uint32](0x02000000 + $off))
        vtable = Hex32 $vtable
        guid = Hex32 $guid
        objectId = ("0x{0:x4}" -f $objectId)
        settings = Hex32 $settings
        stateType = ("0x{0:x4}" -f $stateType)
        flags = Hex32 $flags
        x = Hex32 (Read-U32LE $bytes ($off + 0x5c))
        y = Hex32 (Read-U32LE $bytes ($off + 0x60))
        z = Hex32 (Read-U32LE $bytes ($off + 0x64))
        prevX = Hex32 (Read-U32LE $bytes ($off + 0x68))
        prevY = Hex32 (Read-U32LE $bytes ($off + 0x6c))
        velX = Hex32 (Read-U32LE $bytes ($off + 0x74))
        velY = Hex32 (Read-U32LE $bytes ($off + 0x78))
    })
}

if ($OutCsv) {
    $parent = Split-Path -Parent $OutCsv
    if ($parent) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    $rows | Sort-Object objectId,guid,base | Export-Csv -NoTypeInformation -Encoding UTF8 $OutCsv
}

$rows | Group-Object objectId | Sort-Object Name | Select-Object Name,Count
