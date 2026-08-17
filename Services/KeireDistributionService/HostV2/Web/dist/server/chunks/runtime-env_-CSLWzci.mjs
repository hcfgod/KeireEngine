//#region Source/lib/runtime-env.ts
function runtimeEnvironment(name) {
	const value = process.env[name]?.trim();
	return value ? value : void 0;
}
//#endregion
export { runtimeEnvironment as t };
