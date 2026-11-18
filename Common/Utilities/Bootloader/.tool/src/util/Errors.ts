export class VbootError extends Error {
    constructor(message: string) {
        super(message);
        this.name = new.target.name;
    }
}


export class TimeoutError extends VbootError {}


export class TransportError extends VbootError {}


export class DeviceError extends VbootError {
    public readonly command: number;

    public readonly status: number;


    constructor(command: number, status: number, message: string) {
        super(message);
        this.command = command;
        this.status = status;
    }
}
