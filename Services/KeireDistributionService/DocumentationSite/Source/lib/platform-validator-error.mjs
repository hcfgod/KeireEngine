const errorBrand = Symbol.for("Keire.ValidatorQueueError");

export class ValidatorQueueError extends Error {
    constructor(status, code, message) {
        super(message);
        this.status = status;
        this.code = code;
        Object.defineProperty(this, errorBrand, { value: true });
    }

    static [Symbol.hasInstance](value) {
        return value?.[errorBrand] === true;
    }
}
