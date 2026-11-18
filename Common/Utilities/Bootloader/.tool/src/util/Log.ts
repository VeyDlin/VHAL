export type LogLevel = "quiet" | "normal" | "verbose";


export class Log {
    public level: LogLevel;

    private progressOpen: boolean = false;

    private progressWidth: number = 0;


    constructor(level: LogLevel) {
        this.level = level;
    }


    public info(message: string): void {
        if (this.level === "quiet") {
            return;
        }

        this.endProgressLine();
        process.stdout.write(`${message}\n`);
    }


    public detail(message: string): void {
        if (this.level !== "verbose") {
            return;
        }

        this.endProgressLine();
        process.stdout.write(`${message}\n`);
    }


    public warn(message: string): void {
        this.endProgressLine();
        process.stderr.write(`warning: ${message}\n`);
    }


    public progress(label: string, done: number, total: number): void {
        if (this.level === "quiet") {
            return;
        }

        const percent: number = total > 0 ? Math.min(100, Math.floor((done * 100) / total)) : 100;
        const text: string = `${label} ${percent}% (${done}/${total})`;
        const finished: boolean = done >= total;

        // Without a terminal the line is printed once, when it is complete, instead of being rewritten
        if (!process.stdout.isTTY) {
            if (finished) {
                process.stdout.write(`${text}\n`);
            }

            return;
        }

        // Padding clears the rest of a longer previous line
        const width: number = Math.max(this.progressWidth, text.length);
        process.stdout.write(`\r${text.padEnd(width)}`);
        this.progressOpen = true;
        this.progressWidth = width;

        if (finished) {
            this.endProgressLine();
        }
    }


    private endProgressLine(): void {
        if (!this.progressOpen) {
            return;
        }

        process.stdout.write("\n");
        this.progressOpen = false;
        this.progressWidth = 0;
    }
}
