#Requires -Version 5.1
<#
Board check of the Modbus RTU slave that .demo/STM32G4 runs as DemoModbus: USART1 on the console adapter (a CH343 USB-serial
adapter, COM3 by default), slave address 1, 9600 baud, 8 data bits, even parity, one stop bit, plain TTL.

Run it with Windows PowerShell 5.1:
    powershell -NoProfile -File ModbusBoardCheck.ps1 [-Port COM3]
    powershell -NoProfile -File ModbusBoardCheck.ps1 -SelfTest

-SelfTest opens no port. It checks the CRC against known vectors, then checks the request frames of cases 1 to 13 against the
frames written in this file. The CRCs in those frames were computed with a separate table-driven implementation. The first self-test
vector is the catalogue check value of CRC-16/MODBUS for "123456789" (0x4B37).

Exit code 0 means every case passed on the board (or every self-test check passed).
#>
param(
	[string]$Port = 'COM3',
	[switch]$SelfTest
)

$ErrorActionPreference = 'Stop'

# The state of the board that the cases expect. Holding holds registers 0x0000 to 0x0003, Coils holds coils 0x0000 to 0x0007.
# The cases update them with the values they wrote, so case 14 can check the reads against them.
[int[]]$script:Holding = @(0, 0, 0, 0)
[int[]]$script:Coils = @(0, 0, 0, 0, 0, 0, 0, 0)

# The open port, the bytes of the current case and the frames sent in self-test mode.
[System.IO.Ports.SerialPort]$script:Serial = $null
[bool]$script:DryRun = $false
[byte[]]$script:LastReply = [byte[]]@()
[System.Collections.Generic.List[string]]$script:Trace = New-Object System.Collections.Generic.List[string]
[System.Collections.Generic.List[string]]$script:Sent = New-Object System.Collections.Generic.List[string]
[int]$script:Passed = 0
[int]$script:Failed = 0


# CRC-16/MODBUS written from the specification: reflected polynomial 0xA001, initial value 0xFFFF, no final xor. Each byte is
# added to the register low byte first, and the eight bits of the byte shift out one at a time. The CRC goes on the wire low byte first.
function Get-ModbusCrc {
	param([byte[]]$Data)

	[int]$crc = 0xFFFF
	foreach ($byte in $Data) {
		$crc = $crc -bxor [int]$byte
		for ([int]$bit = 0; $bit -lt 8; $bit++) {
			if (($crc -band 1) -ne 0) {
				$crc = ($crc -shr 1) -bxor 0xA001
			} else {
				$crc = $crc -shr 1
			}
		}
	}

	return $crc
}


# Joins byte arrays and single bytes into one byte array. Each part is enumerated, so a byte array adds all its bytes.
function Join-Bytes {
	param([object[]]$Parts)

	[System.Collections.Generic.List[byte]]$list = New-Object 'System.Collections.Generic.List[byte]'
	foreach ($part in $Parts) {
		foreach ($item in @($part)) {
			$list.Add([byte]$item)
		}
	}

	return ,$list.ToArray()
}


# The two bytes of a 16-bit value, high byte first, as Modbus sends them.
function Get-Word {
	param([int]$Value)

	return ,([byte[]]@([byte](($Value -shr 8) -band 0xFF), [byte]($Value -band 0xFF)))
}


# A complete frame: the data bytes (slave address and PDU) with the CRC appended low byte first.
function New-Frame {
	param([byte[]]$Data)

	[int]$crc = Get-ModbusCrc -Data $Data
	return ,(Join-Bytes @($Data, [byte]($crc -band 0xFF), [byte](($crc -shr 8) -band 0xFF)))
}


# True when the last two bytes of a frame are the CRC of the rest.
function Test-Frame {
	param([byte[]]$Frame)

	if ($null -eq $Frame -or $Frame.Length -lt 4) {
		return $false
	}

	[byte[]]$body = $Frame[0..($Frame.Length - 3)]
	[int]$crc = Get-ModbusCrc -Data $body
	[bool]$lowOk = ($Frame[$Frame.Length - 2] -eq ($crc -band 0xFF))
	[bool]$highOk = ($Frame[$Frame.Length - 1] -eq (($crc -shr 8) -band 0xFF))
	return ($lowOk -and $highOk)
}


# The reply to a read of holding or input registers, as a full frame.
function New-ReadReply {
	param(
		[int]$FunctionCode,
		[int[]]$Values
	)

	[System.Collections.Generic.List[object]]$parts = New-Object System.Collections.Generic.List[object]
	$parts.Add([byte]1)
	$parts.Add([byte]$FunctionCode)
	$parts.Add([byte](2 * $Values.Length))
	foreach ($value in $Values) {
		$parts.Add((Get-Word $value))
	}

	return New-Frame -Data (Join-Bytes $parts.ToArray())
}


# The reply to a request that the board refuses: the function code with bit 7 set, then the exception code.
function New-ExceptionReply {
	param(
		[int]$FunctionCode,
		[int]$ExceptionCode
	)

	return New-Frame -Data (Join-Bytes @(1, (0x80 -bor $FunctionCode), $ExceptionCode))
}


# The hex text of bytes, as "01 03 00", for the output and for comparing frames.
function ConvertTo-HexText {
	param([byte[]]$Bytes)

	if ($null -eq $Bytes -or $Bytes.Length -eq 0) {
		return ''
	}

	return (($Bytes | ForEach-Object { $_.ToString('X2') }) -join ' ')
}


# The bytes of a hex text such as "01 03 00 00 00 0A".
function ConvertFrom-HexText {
	param([string]$Text)

	[System.Collections.Generic.List[byte]]$bytes = New-Object 'System.Collections.Generic.List[byte]'
	foreach ($part in ($Text -split ' ')) {
		if ($part -ne '') {
			$bytes.Add([Convert]::ToByte($part, 16))
		}
	}

	return ,$bytes.ToArray()
}


# Reads the reply of the board. The reply ends when no byte has arrived for 50 ms after the first byte. With no byte at all the wait
# ends at 500 ms. The whole wait is capped at 500 ms, so a board that keeps sending cannot hold the check.
function Read-Reply {
	[System.Collections.Generic.List[byte]]$buffer = New-Object 'System.Collections.Generic.List[byte]'
	[System.Diagnostics.Stopwatch]$total = [System.Diagnostics.Stopwatch]::StartNew()
	[System.Diagnostics.Stopwatch]$quiet = [System.Diagnostics.Stopwatch]::StartNew()

	while ($true) {
		if ($total.ElapsedMilliseconds -ge 500) {
			break
		}

		[int]$available = $script:Serial.BytesToRead
		if ($available -gt 0) {
			[byte[]]$chunk = New-Object byte[] $available
			[int]$count = $script:Serial.Read($chunk, 0, $available)
			for ([int]$index = 0; $index -lt $count; $index++) {
				$buffer.Add($chunk[$index])
			}

			$quiet.Restart()
		} elseif ($buffer.Count -gt 0 -and $quiet.ElapsedMilliseconds -ge 50) {
			break
		} else {
			Start-Sleep -Milliseconds 1
		}
	}

	return ,$buffer.ToArray()
}


# Sends one request and returns the reply (an empty array when the board does not answer). SplitAt sends the first SplitAt bytes,
# waits GapMs, and sends the rest. In self-test mode nothing is sent: the request is recorded in Sent and the reply is empty.
function Invoke-Exchange {
	param(
		[byte[]]$Request,
		[int]$SplitAt = 0,
		[int]$GapMs = 0
	)

	if ($script:DryRun) {
		[void]$script:Sent.Add((ConvertTo-HexText $Request))
		return ,([byte[]]@())
	}

	$script:Serial.DiscardInBuffer()
	if ($SplitAt -gt 0) {
		$script:Serial.Write($Request, 0, $SplitAt)
		Start-Sleep -Milliseconds $GapMs
		$script:Serial.Write($Request, $SplitAt, $Request.Length - $SplitAt)
		$script:Trace.Add('sent     ' + (ConvertTo-HexText $Request) + ' (split after ' + $SplitAt + ' bytes, gap ' + $GapMs + ' ms)')
	} else {
		$script:Serial.Write($Request, 0, $Request.Length)
		$script:Trace.Add('sent     ' + (ConvertTo-HexText $Request))
	}

	[byte[]]$reply = Read-Reply
	[string]$received = ConvertTo-HexText $reply
	if ($received -eq '') {
		$received = '(no reply)'
	}

	$script:Trace.Add('received ' + $received)
	$script:LastReply = $reply
	return ,$reply
}


# Sends one request and returns true when the reply is the expected bytes.
function Test-Exchange {
	param(
		[byte[]]$Request,
		[byte[]]$Expected
	)

	[byte[]]$reply = Invoke-Exchange -Request $Request
	return ((ConvertTo-HexText $reply) -eq (ConvertTo-HexText $Expected))
}


# Prints one verdict with the frames of its case. Self-test mode prints nothing: it only records the frames.
function Write-Verdict {
	param(
		[int]$Number,
		[string]$Name,
		[bool]$Ok,
		[string]$Detail
	)

	if ($script:DryRun) {
		$script:Trace.Clear()
		return
	}

	[string]$label = 'FAIL'
	if ($Ok) {
		$label = 'PASS'
		$script:Passed++
	} else {
		$script:Failed++
	}

	Write-Host ('{0} {1,2}  {2}' -f $label, $Number, $Name)
	if ($Detail -ne '') {
		Write-Host ('          ' + $Detail)
	}

	foreach ($line in $script:Trace) {
		Write-Host ('          ' + $line)
	}

	$script:Trace.Clear()
}


# Case 1: read four holding registers from 0x0000. The values are recorded and become the state of the board.
function Case-01 {
	[byte[]]$request = New-Frame (Join-Bytes @(0x01, 0x03, (Get-Word 0), (Get-Word 4)))
	[byte[]]$reply = Invoke-Exchange -Request $request
	[bool]$ok = (Test-Frame $reply) -and ($reply.Length -eq 13) -and ($reply[1] -eq 3) -and ($reply[2] -eq 8)

	[int[]]$values = @()
	if ($ok) {
		for ([int]$index = 0; $index -lt 4; $index++) {
			$values += ([int]$reply[3 + 2 * $index] * 256) + [int]$reply[4 + 2 * $index]
		}

		$script:Holding = [int[]]$values
	}

	[string]$detail = 'values ' + (($values | ForEach-Object { $_.ToString() }) -join ' ')
	Write-Verdict -Number 1 -Name 'FC03 read 0x0000 count 4: ok, 4 values' -Ok $ok -Detail $detail
}


# Case 2: write 1234 to 0x0000 (echo), then read it back.
function Case-02 {
	[byte[]]$writeRequest = New-Frame (Join-Bytes @(0x01, 0x06, (Get-Word 0), (Get-Word 1234)))
	[bool]$writeOk = Test-Exchange -Request $writeRequest -Expected $writeRequest
	if ($writeOk) {
		$script:Holding[0] = 1234
	}

	[byte[]]$readRequest = New-Frame (Join-Bytes @(0x01, 0x03, (Get-Word 0), (Get-Word 1)))
	[bool]$readOk = Test-Exchange -Request $readRequest -Expected (New-ReadReply -FunctionCode 3 -Values @(1234))
	Write-Verdict -Number 2 -Name 'FC06 write 0x0000 = 1234 (echo); FC03 read 0x0000 = 1234' -Ok ($writeOk -and $readOk) -Detail ''
}


# Case 3: write 0xFFFF to 0x0000 (refused, exception 03), then the value still reads 1234.
function Case-03 {
	[byte[]]$writeRequest = New-Frame (Join-Bytes @(0x01, 0x06, (Get-Word 0), (Get-Word 0xFFFF)))
	[bool]$writeOk = Test-Exchange -Request $writeRequest -Expected (New-ExceptionReply -FunctionCode 0x06 -ExceptionCode 3)

	[byte[]]$readRequest = New-Frame (Join-Bytes @(0x01, 0x03, (Get-Word 0), (Get-Word 1)))
	[bool]$readOk = Test-Exchange -Request $readRequest -Expected (New-ReadReply -FunctionCode 3 -Values @(1234))
	Write-Verdict -Number 3 -Name 'FC06 write 0x0000 = 0xFFFF: exception 03, value stays 1234' -Ok ($writeOk -and $readOk) -Detail ''
}


# Case 4: write 10, 20, 30 to 0x0001 to 0x0003 (FC10, echo of address and count), then read them back.
function Case-04 {
	[byte[]]$writeRequest = New-Frame (Join-Bytes @(0x01, 0x10, (Get-Word 1), (Get-Word 3), 6, (Get-Word 10), (Get-Word 20), (Get-Word 30)))
	[byte[]]$writeExpected = New-Frame (Join-Bytes @(0x01, 0x10, (Get-Word 1), (Get-Word 3)))
	[bool]$writeOk = Test-Exchange -Request $writeRequest -Expected $writeExpected
	if ($writeOk) {
		$script:Holding[1] = 10
		$script:Holding[2] = 20
		$script:Holding[3] = 30
	}

	[byte[]]$readRequest = New-Frame (Join-Bytes @(0x01, 0x03, (Get-Word 1), (Get-Word 3)))
	[bool]$readOk = Test-Exchange -Request $readRequest -Expected (New-ReadReply -FunctionCode 3 -Values @(10, 20, 30))
	Write-Verdict -Number 4 -Name 'FC10 write 0x0001 count 3 = 10 20 30 (echo); read back' -Ok ($writeOk -and $readOk) -Detail ''
}


# Case 5: FC10 at 0x0002 count 3 reaches 0x0004, which the view does not have, so it gets exception 02 and nothing changes.
function Case-05 {
	[byte[]]$writeRequest = New-Frame (Join-Bytes @(0x01, 0x10, (Get-Word 2), (Get-Word 3), 6, (Get-Word 7), (Get-Word 8), (Get-Word 9)))
	[bool]$writeOk = Test-Exchange -Request $writeRequest -Expected (New-ExceptionReply -FunctionCode 0x10 -ExceptionCode 2)

	[byte[]]$readRequest = New-Frame (Join-Bytes @(0x01, 0x03, (Get-Word 0), (Get-Word 4)))
	[bool]$readOk = Test-Exchange -Request $readRequest -Expected (New-ReadReply -FunctionCode 3 -Values @(1234, 10, 20, 30))
	Write-Verdict -Number 5 -Name 'FC10 write 0x0002 count 3 reaches 0x0004: exception 02, nothing changes' -Ok ($writeOk -and $readOk) -Detail ''
}


# Case 6: FC03 at 0x0004 is outside the view: exception 02.
function Case-06 {
	[byte[]]$request = New-Frame (Join-Bytes @(0x01, 0x03, (Get-Word 4), (Get-Word 1)))
	[bool]$ok = Test-Exchange -Request $request -Expected (New-ExceptionReply -FunctionCode 0x03 -ExceptionCode 2)
	Write-Verdict -Number 6 -Name 'FC03 read 0x0004 count 1: exception 02' -Ok $ok -Detail ''
}


# Case 7: FC04 read of the input register 0x0010 twice. The second value is larger, because the count of good frames grows.
function Case-07 {
	[byte[]]$request = New-Frame (Join-Bytes @(0x01, 0x04, (Get-Word 0x10), (Get-Word 1)))
	[byte[]]$first = Invoke-Exchange -Request $request
	[bool]$firstOk = (Test-Frame $first) -and ($first.Length -eq 7) -and ($first[1] -eq 4) -and ($first[2] -eq 2)
	[byte[]]$second = Invoke-Exchange -Request $request
	[bool]$secondOk = (Test-Frame $second) -and ($second.Length -eq 7) -and ($second[1] -eq 4) -and ($second[2] -eq 2)

	[string]$detail = ''
	[bool]$ok = $firstOk -and $secondOk
	if ($ok) {
		[int]$firstValue = ([int]$first[3] * 256) + [int]$first[4]
		[int]$secondValue = ([int]$second[3] * 256) + [int]$second[4]
		$detail = 'first ' + $firstValue + ', second ' + $secondValue
		$ok = $secondValue -gt $firstValue
	}

	Write-Verdict -Number 7 -Name 'FC04 read 0x0010 count 1 twice: second value is larger' -Ok $ok -Detail $detail
}


# Case 8: FC05 coil 3 on, FC0F coils 0 to 7 with the pattern 0xA5, FC01 coils, FC02 discrete inputs. The discrete input n is coil n
# XOR (n is odd), so the expected discrete bits come from the pattern through that rule.
function Case-08 {
	[byte[]]$coilRequest = New-Frame (Join-Bytes @(0x01, 0x05, (Get-Word 3), (Get-Word 0xFF00)))
	[bool]$coilOk = Test-Exchange -Request $coilRequest -Expected $coilRequest

	[int]$pattern = 0xA5
	[int[]]$coils = @()
	for ([int]$index = 0; $index -lt 8; $index++) {
		$coils += (($pattern -shr $index) -band 1)
	}

	$script:Coils = [int[]]$coils
	[byte[]]$writeRequest = New-Frame (Join-Bytes @(0x01, 0x0F, (Get-Word 0), (Get-Word 8), 1, $pattern))
	[byte[]]$writeExpected = New-Frame (Join-Bytes @(0x01, 0x0F, (Get-Word 0), (Get-Word 8)))
	[bool]$writeOk = Test-Exchange -Request $writeRequest -Expected $writeExpected

	[int]$packed = 0
	for ([int]$index = 0; $index -lt 8; $index++) {
		$packed = $packed -bor ($script:Coils[$index] -shl $index)
	}

	[byte[]]$readCoilsRequest = New-Frame (Join-Bytes @(0x01, 0x01, (Get-Word 0), (Get-Word 8)))
	[bool]$readCoilsOk = Test-Exchange -Request $readCoilsRequest -Expected (New-Frame (Join-Bytes @(0x01, 0x01, 0x01, $packed)))

	[int]$discrete = 0
	for ([int]$index = 0; $index -lt 4; $index++) {
		[int]$odd = $index -band 1
		[int]$inputBit = $script:Coils[$index] -bxor $odd
		$discrete = $discrete -bor ($inputBit -shl $index)
	}

	[byte[]]$readDiscreteRequest = New-Frame (Join-Bytes @(0x01, 0x02, (Get-Word 0), (Get-Word 4)))
	[bool]$readDiscreteOk = Test-Exchange -Request $readDiscreteRequest -Expected (New-Frame (Join-Bytes @(0x01, 0x02, 0x01, $discrete)))

	[bool]$ok = $coilOk -and $writeOk -and $readCoilsOk -and $readDiscreteOk
	[string]$detail = 'coils ' + (ConvertTo-HexText ([byte[]]@($packed))) + ', discrete ' + (ConvertTo-HexText ([byte[]]@($discrete)))
	Write-Verdict -Number 8 -Name 'FC05 coil 3 on; FC0F coils 0-7 = A5; FC01 coils; FC02 discrete inputs' -Ok $ok -Detail $detail
}


# Case 9: function code 0x07 is not served: exception 01.
function Case-09 {
	[byte[]]$request = New-Frame (Join-Bytes @(0x01, 0x07))
	[bool]$ok = Test-Exchange -Request $request -Expected (New-ExceptionReply -FunctionCode 0x07 -ExceptionCode 1)
	Write-Verdict -Number 9 -Name 'function code 0x07: exception 01' -Ok $ok -Detail ''
}


# Case 10: a frame with a wrong CRC (the last byte changed) gets no reply.
function Case-10 {
	[byte[]]$good = New-Frame (Join-Bytes @(0x01, 0x03, (Get-Word 0), (Get-Word 1)))
	[byte[]]$bad = $good.Clone()
	$bad[$bad.Length - 1] = $bad[$bad.Length - 1] -bxor 0x01
	[bool]$checkOk = -not (Test-Frame $bad)
	[bool]$ok = $checkOk -and (Test-Exchange -Request $bad -Expected ([byte[]]@()))
	Write-Verdict -Number 10 -Name 'frame with a wrong CRC: no reply' -Ok $ok -Detail ''
}


# Case 11: a request for slave 2 gets no reply.
function Case-11 {
	[byte[]]$request = New-Frame (Join-Bytes @(0x02, 0x03, (Get-Word 0), (Get-Word 1)))
	[bool]$ok = Test-Exchange -Request $request -Expected ([byte[]]@())
	Write-Verdict -Number 11 -Name 'request for slave 2: no reply' -Ok $ok -Detail ''
}


# Case 12: a broadcast FC06 (address 0) writes 4321 to 0x0000 and gets no reply. Slave 1 then reads 4321.
function Case-12 {
	[byte[]]$broadcast = New-Frame (Join-Bytes @(0x00, 0x06, (Get-Word 0), (Get-Word 4321)))
	[bool]$broadcastOk = Test-Exchange -Request $broadcast -Expected ([byte[]]@())
	$script:Holding[0] = 4321

	[byte[]]$readRequest = New-Frame (Join-Bytes @(0x01, 0x03, (Get-Word 0), (Get-Word 1)))
	[bool]$readOk = Test-Exchange -Request $readRequest -Expected (New-ReadReply -FunctionCode 3 -Values @(4321))
	Write-Verdict -Number 12 -Name 'broadcast FC06 0x0000 = 4321: no reply; slave 1 reads 4321' -Ok ($broadcastOk -and $readOk) -Detail ''
}


# Case 13: a valid read sent in two halves with 30 ms between them (more than t3.5 at 9600) gets no reply. A normal request then works.
function Case-13 {
	[byte[]]$request = New-Frame (Join-Bytes @(0x01, 0x03, (Get-Word 0), (Get-Word 1)))
	[byte[]]$split = Invoke-Exchange -Request $request -SplitAt 4 -GapMs 30
	[bool]$splitOk = $split.Length -eq 0

	[bool]$readOk = Test-Exchange -Request $request -Expected (New-ReadReply -FunctionCode 3 -Values @(4321))
	Write-Verdict -Number 13 -Name 'request split in two halves, 30 ms apart: no reply; then a normal request works' -Ok ($splitOk -and $readOk) -Detail ''
}


# Case 14: 500 random FC03 reads of 1 to 4 registers inside 0x0000 to 0x0003. Every reply must be the expected words. The seed is
# printed, so a failing run can be repeated.
function Case-14 {
	if ($script:DryRun) {
		return
	}

	[int]$seed = [int]((Get-Date).Ticks % 2000000000)
	[System.Random]$random = New-Object System.Random -ArgumentList $seed
	[int]$wrong = 0
	[string]$firstWrong = ''
	for ([int]$round = 0; $round -lt 500; $round++) {
		[int]$count = $random.Next(1, 5)
		[int]$start = $random.Next(0, 5 - $count)
		[byte[]]$request = New-Frame (Join-Bytes @(0x01, 0x03, (Get-Word $start), (Get-Word $count)))
		[int[]]$values = @()
		for ([int]$index = 0; $index -lt $count; $index++) {
			$values += $script:Holding[$start + $index]
		}

		[byte[]]$expected = New-ReadReply -FunctionCode 3 -Values ([int[]]$values)
		if (-not (Test-Exchange -Request $request -Expected $expected)) {
			$wrong++
			if ($firstWrong -eq '') {
				$firstWrong = 'round ' + $round + ': sent ' + (ConvertTo-HexText $request) + ', expected ' + (ConvertTo-HexText $expected) + ', received ' + (ConvertTo-HexText $script:LastReply)
			}
		}

		$script:Trace.Clear()
	}

	[string]$detail = 'seed ' + $seed + ', 500 reads, ' + $wrong + ' wrong'
	if ($firstWrong -ne '') {
		$detail = $detail + '; first: ' + $firstWrong
	}

	Write-Verdict -Number 14 -Name 'random FC03 reads of 1 to 4 registers inside 0x0000 to 0x0003 (500)' -Ok ($wrong -eq 0) -Detail $detail
}


# The request frames of cases 1 to 13, in the order they are sent. Each is written out here in full, with its CRC. The CRCs came from
# a separate table-driven implementation. Entry 18 is the frame with the wrong CRC, which must fail the check. Entries 22 and 23 are
# the split frame of case 13 and the read after it.
function Get-HandFrames {
	return @(
		'01 03 00 00 00 04 44 09',
		'01 06 00 00 04 D2 0B 57',
		'01 03 00 00 00 01 84 0A',
		'01 06 00 00 FF FF 88 7A',
		'01 03 00 00 00 01 84 0A',
		'01 10 00 01 00 03 06 00 0A 00 14 00 1E EF 48',
		'01 03 00 01 00 03 54 0B',
		'01 10 00 02 00 03 06 00 07 00 08 00 09 B3 4E',
		'01 03 00 00 00 04 44 09',
		'01 03 00 04 00 01 C5 CB',
		'01 04 00 10 00 01 30 0F',
		'01 04 00 10 00 01 30 0F',
		'01 05 00 03 FF 00 7C 3A',
		'01 0F 00 00 00 08 01 A5 3E EE',
		'01 01 00 00 00 08 3D CC',
		'01 02 00 00 00 04 79 C9',
		'01 07 41 E2',
		'01 03 00 00 00 01 84 0B',
		'02 03 00 00 00 01 84 39',
		'00 06 00 00 10 E1 45 93',
		'01 03 00 00 00 01 84 0A',
		'01 03 00 00 00 01 84 0A',
		'01 03 00 00 00 01 84 0A'
	)
}


# Self-test: the CRC vectors, then the request frames of cases 1 to 13 against the table above. Nothing is sent.
function Invoke-SelfTest {
	$script:DryRun = $true
	[bool]$allOk = $true
	[int]$checks = 0
	[int]$passes = 0

	$vectors = @(
		@{
			Name = 'CRC-16/MODBUS check value of "123456789"'
			Data = '31 32 33 34 35 36 37 38 39'
			Frame = '31 32 33 34 35 36 37 38 39 37 4B'
		},
		@{
			Name = 'CRC of 01 03 00 00 00 0A'
			Data = '01 03 00 00 00 0A'
			Frame = '01 03 00 00 00 0A C5 CD'
		},
		@{
			Name = 'CRC of 01 03 00 00 00 01'
			Data = '01 03 00 00 00 01'
			Frame = '01 03 00 00 00 01 84 0A'
		},
		@{
			Name = 'CRC of 01 06 00 01 00 03'
			Data = '01 06 00 01 00 03'
			Frame = '01 06 00 01 00 03 98 0B'
		},
		@{
			Name = 'CRC of 01 03 00 00 00 02'
			Data = '01 03 00 00 00 02'
			Frame = '01 03 00 00 00 02 C4 0B'
		},
		@{
			Name = 'CRC of 01 07'
			Data = '01 07'
			Frame = '01 07 41 E2'
		}
	)

	foreach ($vector in $vectors) {
		$checks++
		[string]$built = ConvertTo-HexText (New-Frame (ConvertFrom-HexText $vector.Data))
		[bool]$ok = $built -eq $vector.Frame
		if ($ok) {
			$passes++
			Write-Host ('PASS  ' + $vector.Name + ': ' + $built)
		} else {
			$allOk = $false
			Write-Host ('FAIL  ' + $vector.Name + ': built ' + $built + ', expected ' + $vector.Frame)
		}
	}

	$script:Sent.Clear()
	Case-01
	Case-02
	Case-03
	Case-04
	Case-05
	Case-06
	Case-07
	Case-08
	Case-09
	Case-10
	Case-11
	Case-12
	Case-13

	[string[]]$hand = Get-HandFrames
	[bool]$framesOk = $script:Sent.Count -eq $hand.Count
	[string]$frameDetail = ''
	for ([int]$index = 0; $index -lt $hand.Count; $index++) {
		$checks++
		[string]$expectedFrame = $hand[$index]
		[string]$sentFrame = ''
		if ($index -lt $script:Sent.Count) {
			$sentFrame = $script:Sent[$index]
		}

		[bool]$crcOk = Test-Frame (ConvertFrom-HexText $expectedFrame)
		[bool]$isWrongCrc = $index -eq 17
		[bool]$ok = $false
		if ($isWrongCrc) {
			$ok = (-not $crcOk) -and ($sentFrame -eq $expectedFrame)
		} else {
			$ok = $crcOk -and ($sentFrame -eq $expectedFrame)
		}

		if ($ok) {
			$passes++
		} else {
			$framesOk = $false
			$frameDetail = $frameDetail + ('FAIL frame ' + ($index + 1) + ': built ' + $sentFrame + ', expected ' + $expectedFrame + "`n")
		}
	}

	if ($framesOk) {
		Write-Host ('PASS  request frames of cases 1 to 13: ' + $hand.Count + ' of ' + $hand.Count + ' match the hand-computed bytes')
	} else {
		$allOk = $false
		Write-Host 'FAIL  request frames of cases 1 to 13'
		Write-Host $frameDetail
	}

	$checks++
	if ($framesOk) {
		$passes++
	}

	$script:DryRun = $false
	Write-Host ('Self-test: ' + $passes + ' of ' + $checks + ' checks passed')
	return $allOk
}


if ($SelfTest) {
	if (Invoke-SelfTest) {
		exit 0
	}

	exit 1
}

[int]$exitCode = 1
[System.IO.Ports.SerialPort]$serial = New-Object System.IO.Ports.SerialPort -ArgumentList $Port, 9600, ([System.IO.Ports.Parity]::Even), 8, ([System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 100
$serial.WriteTimeout = 1000
try {
	$serial.Open()
	$script:Serial = $serial
	Write-Host ('Board check on ' + $Port + ' at 9600 8E1, slave 1')

	Case-01
	Case-02
	Case-03
	Case-04
	Case-05
	Case-06
	Case-07
	Case-08
	Case-09
	Case-10
	Case-11
	Case-12
	Case-13
	Case-14

	[int]$total = $script:Passed + $script:Failed
	Write-Host ('Summary: ' + $script:Passed + ' of ' + $total + ' cases passed')
	if ($script:Failed -eq 0) {
		Write-Host 'RESULT: PASS'
		$exitCode = 0
	} else {
		Write-Host 'RESULT: FAIL'
	}
} finally {
	if ($serial.IsOpen) {
		$serial.Close()
	}

	$serial.Dispose()
}

exit $exitCode
