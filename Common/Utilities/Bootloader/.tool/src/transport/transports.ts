import { TransportRegistry } from "./TransportRegistry.ts";
import { bleTransport } from "./ble/BleTransport.ts";
import { simTransport } from "./sim/SimTransport.ts";


// Adding a transport: one folder under src/transport/ exporting a TransportFactory, and one register line here
export function createRegistry(): TransportRegistry {
    const registry: TransportRegistry = new TransportRegistry();

    registry.register(bleTransport);
    registry.register(simTransport);

    return registry;
}
