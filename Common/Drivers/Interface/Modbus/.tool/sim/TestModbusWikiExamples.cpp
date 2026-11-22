// Every snippet of the Modbus wiki pages compiles here. The G4 adapters are not built on the host, so FakeLine stands in for
// UARTAdapterG4 and FakeGpio for GPIOAdapterG4; the snippets are otherwise copied as they are on the pages. Each page's snippets have
// their own namespace, because they reuse names. The functions are never called: this test checks that the code compiles.
//
// The unused-variable warnings are off for this file only: the snippets keep the results of their calls, as the pages show them.
// The snippet functions are inline, so the compiler does not warn that nothing calls them.
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"

#include "FakeGpio.h"
#include "FakeLine.h"
#include <Drivers/Interface/Modbus/ModbusBroadcast.h>
#include <Drivers/Interface/Modbus/ModbusClient.h>
#include <Drivers/Interface/Modbus/ModbusRegisterBus.h>
#include <Drivers/Interface/Modbus/ModbusResult.h>
#include <Drivers/Interface/Modbus/ModbusServer.h>
#include <Drivers/Interface/Modbus/RTU/RtuFramer.h>
#include <Drivers/Interface/Modbus/RTU/RtuTransport.h>
#include <Utilities/Data/RegisterMap/BitField.h>
#include <Utilities/Data/RegisterMap/RegisterAccess.h>
#include <Utilities/Data/RegisterMap/RegisterDescriptor.h>
#include <Utilities/Data/RegisterMap/RegisterView.h>
#include <Utilities/DataTypes.h>
#include <Utilities/Result/Result.h>
#include <Utilities/Result/ResultStatus.h>
#include <array>
#include <span>


// The clock the pages name. The sim has one: the simulated time.
uint32 MicrosecondClock() {
	return SimClockUs();
}


// ---- Modbus README: the master ----
namespace PageModbusMaster {
	using UARTAdapterG4 = FakeLine;
	using GPIOAdapterG4 = FakeGpio;


	// The transport: one UART, one direction pin. The clock is the port's microsecond clock.
	using Link = RtuTransport<UARTAdapterG4, GPIOAdapterG4, +[] {
		return RtuBuilder()
			.Turnaround(2)
			.Clock<&MicrosecondClock>();
	}>;


	void Run() {
		UARTAdapterG4 uart;
		GPIOAdapterG4 dePin;
		Link link(uart, &dePin);

		// The client: slave 1, 500 ms per attempt, two retries.
		using Plc = ModbusClient<Link, RtuFramer, +[] {
			return ClientBuilder()
				.SlaveAddress(1)
				.Timeout(500)
				.Retries(2);
		}>;

		Plc plc(link);

		std::array<uint16, 4> values{};
		ModbusResult reply = plc.ReadHoldingRegisters(0x0000, values);
	}
}


// ---- Modbus README: the slave ----
namespace PageModbusSlave {
	using UARTAdapterG4 = FakeLine;
	using GPIOAdapterG4 = FakeGpio;


	using Link = RtuTransport<UARTAdapterG4, GPIOAdapterG4, +[] {
		return RtuBuilder()
			.Turnaround(2)
			.Clock<&MicrosecondClock>();
	}>;


	uint16 setpoint = 0;
	bool CheckSetpoint(uint16 value) {
		return value <= 100;
	}


	uint16 ReadChipTemperature() {
		return 0x0042;
	}


	using Table = RegisterView<+[] {
		return RegisterViewBuilder()
			.HoldingRegisters(0x0000, Variable(&setpoint, &CheckSetpoint))
			.InputRegisters(0x0010, Computed(&ReadChipTemperature));
	}>;


	using Server = ModbusServer<Link, RtuFramer, Table, +[] {
		return ServerBuilder()
			.SlaveAddress(1);
	}>;


	void Run() {
		UARTAdapterG4 uart;
		GPIOAdapterG4 dePin;
		Link link(uart, &dePin);
		Server server(link);
		ResultStatus status = server.Process();
	}
}


// ---- RTU README: setup ----
namespace PageRtuSetup {
	using UARTAdapterG4 = FakeLine;
	using GPIOAdapterG4 = FakeGpio;


	using Link = RtuTransport<UARTAdapterG4, GPIOAdapterG4, +[] {
		return RtuBuilder()
			.Turnaround(2)
			.Clock<&MicrosecondClock>();
	}>;


	void Run() {
		UARTAdapterG4 uart;
		GPIOAdapterG4 dePin;
		Link link(uart, &dePin);

		uart.SetParameters({ .baudRate = 9600 });
		link.Begin();
	}
}


// ---- RTU README: lenient frames ----
namespace PageRtuLenient {
	using UARTAdapterG4 = FakeLine;
	using GPIOAdapterG4 = FakeGpio;


	using Link = RtuTransport<UARTAdapterG4, GPIOAdapterG4, +[] {
		return RtuBuilder()
			.Lenient()
			.Clock<&MicrosecondClock>();
	}>;
}


// ---- RtuTransport page: the three ways to pass the pins ----
namespace PageRtuTransport {
	using UARTAdapterG4 = FakeLine;
	using GPIOAdapterG4 = FakeGpio;


	using Link = RtuTransport<UARTAdapterG4, GPIOAdapterG4, +[] {
		return RtuBuilder()
			.Clock<&MicrosecondClock>();
	}>;


	void Run() {
		UARTAdapterG4 uart;
		GPIOAdapterG4 dePin;
		GPIOAdapterG4 rePin;
		Link dePinOnly(uart, &dePin);
		Link bothPins(uart, &dePin, &rePin);
		Link noPins(uart);
	}
}


// ---- ModbusClient page: settings, reads and writes ----
namespace PageModbusClient {
	using UARTAdapterG4 = FakeLine;
	using GPIOAdapterG4 = FakeGpio;


	using Link = RtuTransport<UARTAdapterG4, GPIOAdapterG4, +[] {
		return RtuBuilder()
			.Clock<&MicrosecondClock>();
	}>;


	using Plc = ModbusClient<Link, RtuFramer, +[] {
		return ClientBuilder()
			.SlaveAddress(1)
			.Timeout(500)
			.Retries(2);
	}>;


	inline void Run(Link& link) {
		Plc plc(link);

		std::array<uint16, 4> values{};
		ModbusResult read = plc.ReadHoldingRegisters(0x0000, values);
		if (read.status == ResultStatus::error) {
			uint8 code = read.exception;
		}

		plc.WriteSingleRegister(0x0010, 1234);
		std::array<uint16, 2> pair = {1, 2};
		plc.WriteMultipleRegisters(0x0020, pair);

		std::array<bool, 8> coils{};
		plc.ReadCoils(0x0000, coils);
		plc.WriteSingleCoil(0x0003, true);
	}


	struct MutexLock {
		void Lock() {
		}


		void Unlock() {
		}
	};


	using SharedPlc = ModbusClient<Link, RtuFramer, +[] {
		return ClientBuilder()
			.SlaveAddress(1)
			.Lock();
	}, MutexLock>;


	inline void RunLocked(Link& link) {
		MutexLock plcLock;
		SharedPlc plc(link, &plcLock);
	}
}


// ---- ModbusServer page ----
namespace PageModbusServer {
	using UARTAdapterG4 = FakeLine;
	using GPIOAdapterG4 = FakeGpio;


	using Link = RtuTransport<UARTAdapterG4, GPIOAdapterG4, +[] {
		return RtuBuilder()
			.Clock<&MicrosecondClock>();
	}>;


	uint16 setpoint = 0;
	bool CheckSetpoint(uint16 value) {
		return value <= 100;
	}


	uint16 ReadChipTemperature() {
		return 0x0042;
	}


	bool relayOn = false;


	using Table = RegisterView<+[] {
		return RegisterViewBuilder()
			.HoldingRegisters(0x0000, Variable(&setpoint, &CheckSetpoint))
			.InputRegisters(0x0010, Computed(&ReadChipTemperature))
			.Coils(0x0000, Variable(&relayOn));
	}>;


	using Server = ModbusServer<Link, RtuFramer, Table, +[] {
		return ServerBuilder()
			.SlaveAddress(1)
			.MaxCoilWrite(64);
	}>;


	inline void Run(Link& link) {
		Server server(link);
		ResultStatus status = server.Process();
	}
}


// ---- ModbusBroadcast page ----
namespace PageModbusBroadcast {
	using UARTAdapterG4 = FakeLine;
	using GPIOAdapterG4 = FakeGpio;


	using Link = RtuTransport<UARTAdapterG4, GPIOAdapterG4, +[] {
		return RtuBuilder()
			.Clock<&MicrosecondClock>();
	}>;


	using Plc = ModbusClient<Link, RtuFramer, +[] {
		return ClientBuilder()
			.SlaveAddress(1);
	}>;


	inline void Run(Plc& plc) {
		std::array<bool, 4> coils{};
		ModbusResult sent = plc.Broadcast().WriteSingleRegister(0x0000, 1234);
		plc.Broadcast().WriteMultipleCoils(0x0010, coils);
	}
}


// ---- ModbusRegisterBus page ----
namespace PageModbusRegisterBus {
	using UARTAdapterG4 = FakeLine;
	using GPIOAdapterG4 = FakeGpio;


	using Link = RtuTransport<UARTAdapterG4, GPIOAdapterG4, +[] {
		return RtuBuilder()
			.Clock<&MicrosecondClock>();
	}>;


	using Plc = ModbusClient<Link, RtuFramer, +[] {
		return ClientBuilder()
			.SlaveAddress(1);
	}>;


	inline void Run(Plc& plc) {
		ModbusRegisterBus<Plc> bus(plc);

		constexpr auto Setpoint = RegisterDescriptor<uint16>(0, 0x0000).Table(RegisterTable::Holding);
		constexpr auto Output = RegisterDescriptor<uint8>(0, 0x0005).Table(RegisterTable::Coil);
		constexpr auto Alarm = RegisterDescriptor<uint8>(0, 0x0006).Table(RegisterTable::Discrete).ReadOnly();

		Result<uint16> value = Read<Setpoint>(bus);
		Write<Setpoint>(bus, 100);
		Write<Output>(bus, 1);
		Result<uint8> state = Read<Alarm>(bus);
	}
}


int main() {
	std::printf("TestModbusWikiExamples: every snippet compiled\n");
	return 0;
}
