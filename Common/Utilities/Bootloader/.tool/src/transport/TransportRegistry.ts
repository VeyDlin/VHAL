import { VbootError } from "../util/Errors.ts";
import type { TransportFactory } from "./Transport.ts";


export class TransportRegistry {
    private readonly factories: Map<string, TransportFactory> = new Map<string, TransportFactory>();


    public register(factory: TransportFactory): void {
        if (this.factories.has(factory.name)) {
            throw new VbootError(`transport "${factory.name}" is registered twice`);
        }

        this.factories.set(factory.name, factory);
    }


    public get(name: string): TransportFactory {
        const factory: TransportFactory | undefined = this.factories.get(name);
        if (factory === undefined) {
            const known: string = this.factories.size > 0 ? Array.from(this.factories.keys()).join(", ") : "(none)";
            throw new VbootError(`unknown transport "${name}"; known transports: ${known}`);
        }

        return factory;
    }


    public list(): TransportFactory[] {
        return Array.from(this.factories.values());
    }
}
