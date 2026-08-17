import { n as __reExport, t as __exportAll } from "./rolldown-runtime_BBjsoOtd.mjs";
import { n as BuiltInDefaultLocale, r as user_config_default, t as useTranslations } from "./translations_D8srSkSz.mjs";
import rehypeExpressiveCode, { ExpressiveCodeBlock, createRenderer, getStableObjectHash, stableStringify } from "rehype-expressive-code";
import { fileURLToPath } from "url-extras";
//#region node_modules/astro-expressive-code/dist/hast.js
var hast_exports = /* @__PURE__ */ __exportAll({});
import * as import_rehype_expressive_code_hast from "rehype-expressive-code/hast";
__reExport(hast_exports, import_rehype_expressive_code_hast);
//#endregion
//#region node_modules/astro-expressive-code/dist/index.js
var dist_exports = /* @__PURE__ */ __exportAll({
	astroExpressiveCode: () => astroExpressiveCode,
	createAstroRenderer: () => createAstroRenderer,
	default: () => src_default,
	defineEcConfig: () => defineEcConfig,
	mergeEcConfigOptions: () => mergeEcConfigOptions
});
import * as import_rehype_expressive_code from "rehype-expressive-code";
__reExport(dist_exports, import_rehype_expressive_code);
function serializePartialAstroConfig(config) {
	const partialConfig = {
		base: config.base,
		root: config.root,
		srcDir: config.srcDir
	};
	if (config.build) {
		partialConfig.build = {};
		if (config.build.assets) partialConfig.build.assets = config.build.assets;
		if (config.build.assetsPrefix) partialConfig.build.assetsPrefix = config.build.assetsPrefix;
	}
	if (config.markdown?.shikiConfig?.langs) partialConfig.markdown = { shikiConfig: { langs: config.markdown.shikiConfig.langs } };
	return JSON.stringify(partialConfig);
}
function isSatteriProcessor(processor) {
	if (typeof processor !== "object" || processor === null) return false;
	const candidate = processor;
	return candidate.name === "satteri" && Array.isArray(candidate.options?.hastPlugins);
}
function isUnifiedProcessor(processor) {
	if (typeof processor !== "object" || processor === null) return false;
	const candidate = processor;
	return candidate.name === "unified" && Array.isArray(candidate.options?.rehypePlugins);
}
function getAssetsPrefix(fileExtension, assetsPrefix) {
	if (!assetsPrefix) return "";
	if (typeof assetsPrefix === "string") return assetsPrefix;
	const dotLessFileExtension = fileExtension.slice(1);
	if (assetsPrefix[dotLessFileExtension]) return assetsPrefix[dotLessFileExtension];
	return assetsPrefix.fallback;
}
function getAssetsBaseHref(fileExtension, assetsPrefix, base) {
	return (getAssetsPrefix(fileExtension, assetsPrefix) || base || "").trim().replace(/\/+$/g, "");
}
function getEcConfigFileUrl(projectRootUrl) {
	return new URL("./ec.config.mjs", projectRootUrl);
}
async function loadEcConfigFile(projectRootUrl) {
	const pathsToTry = [new URL(`./ec.config.mjs?t=${Date.now()}`, projectRootUrl).href];
	if (Object.assign({
		"ASSETS_PREFIX": void 0,
		"BASE_URL": "/",
		"DEV": false,
		"MODE": "production",
		"PROD": true,
		"SITE": "https://keireengine.duckdns.org",
		"SSR": true
	}, { Path: "C:\\Users\\keith\\Desktop\\KéireEngine\\Services\\KeireDistributionService\\DocumentationSite\\node_modules\\.bin;C:\\Users\\keith\\Desktop\\KéireEngine\\Services\\KeireDistributionService\\node_modules\\.bin;C:\\Users\\keith\\Desktop\\KéireEngine\\Services\\node_modules\\.bin;C:\\Users\\keith\\Desktop\\KéireEngine\\node_modules\\.bin;C:\\Users\\keith\\Desktop\\node_modules\\.bin;C:\\Users\\keith\\node_modules\\.bin;C:\\Users\\node_modules\\.bin;C:\\node_modules\\.bin;C:\\Program Files\\nodejs\\node_modules\\npm\\node_modules\\@npmcli\\run-script\\lib\\node-gyp-bin;C:\\Users\\keith\\.codex\\tmp\\arg0\\codex-arg0o3rGQT;C:\\Users\\keith\\.cache\\codex-runtimes\\codex-primary-runtime\\dependencies\\bin\\override;C:\\Program Files\\Common Files\\Oracle\\Java\\javapath;C:\\Program Files (x86)\\Common Files\\Oracle\\Java\\java8path;C:\\Program Files (x86)\\Common Files\\Oracle\\Java\\javapath;C:\\Windows\\system32;C:\\Windows;C:\\Windows\\System32\\Wbem;C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\;C:\\Windows\\System32\\OpenSSH\\;C:\\Program Files\\dotnet\\;C:\\Program Files\\cursor\\resources\\app\\bin;C:\\Program Files\\NVIDIA Corporation\\NVIDIA App\\NvDLISR;C:\\Program Files (x86)\\NVIDIA Corporation\\PhysX\\Common;C:\\Program Files\\Cloudflare\\Cloudflare WARP\\;D:\\Windows Kits\\10\\Windows Performance Toolkit\\;C:\\Users\\keith\\AppData\\Local\\Muse Hub\\lib;C:\\Program Files\\CMake\\bin;C:\\Program Files\\GitHub CLI\\;D:\\Program Files\\Git\\cmd;C:\\Program Files\\nodejs\\;C:\\Users\\keith\\.local\\bin;C:\\Users\\keith\\AppData\\Local\\Microsoft\\WindowsApps;C:\\Users\\keith\\.dotnet\\tools;D:\\Users\\keith\\AppData\\Local\\Programs\\cursor\\resources\\app\\bin;D:\\Users\\keith\\AppData\\Local\\Programs\\Windsurf\\bin;C:\\Users\\keith\\AppData\\Local\\Microsoft\\WinGet\\Packages\\AlDanial.Cloc_Microsoft.Winget.Source_8wekyb3d8bbwe;C:\\Users\\keith\\AppData\\Local\\Python\\bin;C:\\Users\\keith\\AppData\\Local\\Programs\\Ollama;C:\\Users\\keith\\.lmstudio\\bin;D:\\Program Files\\JetBrains\\IntelliJ IDEA 2026.1\\bin;C:\\Users\\keith\\AppData\\Local\\Microsoft\\WinGet\\Packages\\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe;C:\\Users\\keith\\.dotnet\\tools;C:\\Users\\keith\\AppData\\Local\\Microsoft\\WinGet\\Links;C:\\Users\\keith\\AppData\\Roaming\\npm;C:\\Users\\keith\\.cache\\codex-runtimes\\codex-primary-runtime\\dependencies\\bin\\fallback;C:\\Users\\keith\\.cache\\codex-runtimes\\codex-primary-runtime\\dependencies\\native\\git\\cmd;C:\\Users\\keith\\AppData\\Local\\OpenAI\\Codex\\bin\\fda9f910656130f4;C:\\Program Files\\WindowsApps\\OpenAI.Codex_26.803.5235.0_x64__2p2nqsd0c76g0\\app\\resources" })?.BASE_URL?.length) pathsToTry.push(`/ec.config.mjs?t=${Date.now()}`);
	function coerceError(error) {
		if (typeof error === "object" && error !== null && "message" in error) return error;
		return { message: error };
	}
	for (const path of pathsToTry) try {
		const module = await import(
			/* @vite-ignore */
			path
);
		if (!module.default) throw new Error(`Missing or invalid default export. Please export your Expressive Code config object as the default export.`);
		return module.default;
	} catch (error) {
		const { message, code } = coerceError(error);
		if (code === "ERR_MODULE_NOT_FOUND" || code === "ERR_LOAD_URL") {
			if (message.replace(/(imported )?from .*$/, "").includes("ec.config.mjs")) continue;
		}
		throw new Error(`Your project includes an Expressive Code config file ("ec.config.mjs")
				that could not be loaded due to ${code ? `the error ${code}` : "the following error"}: ${message}`.replace(/\s+/g, " "), error instanceof Error ? { cause: error } : void 0);
	}
	return {};
}
function mergeEcConfigOptions(...configs) {
	const merged = {};
	configs.forEach((config) => merge(merged, config, [
		"defaultProps",
		"frames",
		"shiki",
		"styleOverrides"
	]));
	return merged;
	function isObject(value) {
		return value !== null && typeof value === "object" && !Array.isArray(value);
	}
	function merge(target, source, limitDeepMergeTo, path = "") {
		for (const key in source) {
			const srcProp = source[key];
			const tgtProp = target[key];
			if (isObject(srcProp)) {
				if (isObject(tgtProp) && (!limitDeepMergeTo || limitDeepMergeTo.includes(key))) merge(tgtProp, srcProp, void 0, path ? path + "." + key : key);
				else target[key] = { ...srcProp };
			} else if (Array.isArray(srcProp)) {
				if (Array.isArray(tgtProp) && path === "shiki" && key === "langs") target[key] = [...tgtProp, ...srcProp];
				else target[key] = [...srcProp];
			} else target[key] = srcProp;
		}
	}
}
async function createAstroRenderer({ ecConfig, astroConfig, logger }) {
	const { emitExternalStylesheet = true, customCreateRenderer, plugins = [], shiki = true, ...rest } = ecConfig ?? {};
	const assetsDir = astroConfig.build?.assets || "_astro";
	let inlineStyles = "";
	const hashedStyles = [];
	const hashedScripts = [];
	plugins.push({
		name: "astro-expressive-code",
		hooks: { postprocessRenderedBlockGroup: ({ renderData, renderedGroupContents }) => {
			if (!(renderedGroupContents[0]?.codeBlock.parentDocument?.positionInDocument?.groupIndex === 0)) return;
			const extraElements = [];
			hashedStyles.forEach(([hashedRoute]) => {
				extraElements.push({
					type: "element",
					tagName: "link",
					properties: {
						rel: "stylesheet",
						href: `${getAssetsBaseHref(".css", astroConfig.build?.assetsPrefix, astroConfig.base)}${hashedRoute}`
					},
					children: []
				});
			});
			if (inlineStyles) extraElements.push({
				type: "element",
				tagName: "style",
				properties: {},
				children: [{
					type: "text",
					value: inlineStyles
				}]
			});
			hashedScripts.forEach(([hashedRoute]) => {
				extraElements.push({
					type: "element",
					tagName: "script",
					properties: {
						type: "module",
						src: `${getAssetsBaseHref(".js", astroConfig.build?.assetsPrefix, astroConfig.base)}${hashedRoute}`
					},
					children: []
				});
			});
			if (!extraElements.length) return;
			renderData.groupAst.children.unshift(...extraElements);
		} }
	});
	const mergedShikiConfig = shiki === true ? {} : shiki;
	const astroShikiConfig = astroConfig.markdown?.shikiConfig;
	if (mergedShikiConfig) {
		if (!mergedShikiConfig.langs && astroShikiConfig?.langs) mergedShikiConfig.langs = astroShikiConfig.langs;
		if (!mergedShikiConfig.langAlias && astroShikiConfig?.langAlias) mergedShikiConfig.langAlias = astroShikiConfig.langAlias;
	}
	const renderer = await (customCreateRenderer ?? createRenderer)({
		plugins,
		logger,
		shiki: mergedShikiConfig,
		...rest
	});
	renderer.hashedStyles = hashedStyles;
	renderer.hashedScripts = hashedScripts;
	if (emitExternalStylesheet) {
		const combinedStyles = `${renderer.baseStyles}${renderer.themeStyles}`;
		hashedStyles.push(getHashedRouteWithContent(combinedStyles, `/${assetsDir}/ec.{hash}.css`));
	} else inlineStyles = `${renderer.baseStyles}${renderer.themeStyles}`;
	renderer.baseStyles = "";
	renderer.themeStyles = "";
	const mergedJsCode = [...new Set(renderer.jsModules)].join("\n");
	renderer.jsModules = [];
	hashedScripts.push(getHashedRouteWithContent(mergedJsCode, `/${assetsDir}/ec.{hash}.js`));
	return renderer;
}
function getHashedRouteWithContent(content, routeTemplate) {
	const contentHash = getStableObjectHash(content, { hashLength: 5 });
	return [routeTemplate.replace("{hash}", contentHash), content];
}
function satteriExpressiveCodePlugin(options) {
	const { tabWidth = 2, getBlockLocale, customCreateRenderer } = options;
	const customCreateBlock = createSatteriBlockFactory(options.customCreateBlock);
	let asyncRenderer;
	let firstBlockClaimed = false;
	return {
		name: "astro-expressive-code-satteri",
		element: {
			filter: ["pre"],
			async visit(node, ctx) {
				const codeBlockInfo = getCodeBlockInfo(node);
				if (!codeBlockInfo) return;
				const isFirstBlock = !firstBlockClaimed;
				firstBlockClaimed = true;
				if (asyncRenderer === void 0) asyncRenderer = (customCreateRenderer ?? createRenderer)(options);
				const { ec, baseStyles, themeStyles, jsModules } = await asyncRenderer;
				let normalizedCode = codeBlockInfo.text;
				if (tabWidth > 0) normalizedCode = normalizedCode.replace(/\t/g, " ".repeat(tabWidth));
				const file = createSatteriDocumentFile(ctx);
				const input = {
					code: normalizedCode,
					language: codeBlockInfo.lang,
					meta: codeBlockInfo.meta,
					parentDocument: { sourceFilePath: file.path }
				};
				if (getBlockLocale) input.locale = await getBlockLocale({
					input,
					file
				});
				const codeBlock = await customCreateBlock({
					input,
					file
				});
				const { renderedGroupAst, styles } = await ec.render(codeBlock);
				const extraElements = [];
				const stylesToPrepend = [];
				if (isFirstBlock) {
					if (baseStyles) stylesToPrepend.push(baseStyles);
					if (themeStyles) stylesToPrepend.push(themeStyles);
				}
				stylesToPrepend.push(...styles);
				if (stylesToPrepend.length) extraElements.push({
					type: "element",
					tagName: "style",
					properties: {},
					children: [{
						type: "text",
						value: stylesToPrepend.join("")
					}]
				});
				if (isFirstBlock) jsModules.forEach((moduleCode) => {
					extraElements.push({
						type: "element",
						tagName: "script",
						properties: { type: "module" },
						children: [{
							type: "text",
							value: moduleCode
						}]
					});
				});
				if (extraElements.length) {
					const firstChild = renderedGroupAst.children.length > 0 ? renderedGroupAst.children[0] : void 0;
					const insertIndex = firstChild?.type === "element" && ["style", "link"].includes(firstChild.tagName) ? 1 : 0;
					renderedGroupAst.children.splice(insertIndex, 0, ...extraElements);
				}
				return renderedGroupAst;
			}
		}
	};
}
function getFilePath(fileURL) {
	if (fileURL.protocol !== "file:") return;
	return fileURLToPath(fileURL);
}
function createSatteriDocumentFile(ctx) {
	const fileURL = ctx.fileURL;
	return {
		url: fileURL,
		path: (fileURL ? getFilePath(fileURL) : void 0) || "",
		cwd: typeof process !== "undefined" ? process.cwd() : "/",
		data: { satteri: {
			source: ctx.source,
			fileURL
		} }
	};
}
function createSatteriBlockFactory(userCreateBlock) {
	const groupIndexByDocument = /* @__PURE__ */ new Map();
	return async ({ input, file }) => {
		const documentKey = input.parentDocument?.sourceFilePath || file.path || "";
		const groupIndex = groupIndexByDocument.get(documentKey) ?? 0;
		groupIndexByDocument.set(documentKey, groupIndex + 1);
		input.parentDocument = {
			...input.parentDocument,
			positionInDocument: { groupIndex }
		};
		if (userCreateBlock) return userCreateBlock({
			input,
			file
		});
		return new ExpressiveCodeBlock(input);
	};
}
function getCodeBlockInfo(pre) {
	if (pre.tagName !== "pre") return;
	const [code, ...rest] = pre.children;
	if (rest.length || !code || code.type !== "element" || code.tagName !== "code") return;
	const [text] = code.children;
	if (!text || text.type !== "text") return;
	const data = code.data;
	return {
		lang: data?.lang ?? "",
		text: text.value,
		meta: data?.meta ?? ""
	};
}
function vitePluginAstroExpressiveCode({ styles, scripts, ecIntegrationOptions, processedEcConfig, astroConfig, command }) {
	const modules = {};
	const configModuleContents = [];
	configModuleContents.push(`export const astroConfig = ${serializePartialAstroConfig(astroConfig)}`);
	const { customConfigPreprocessors, ...otherEcIntegrationOptions } = ecIntegrationOptions;
	configModuleContents.push(`export const ecIntegrationOptions = ${stableStringify(otherEcIntegrationOptions)}`);
	const strEcConfigFileUrlHref = JSON.stringify(getEcConfigFileUrl(astroConfig.root).href);
	configModuleContents.push(`let ecConfigFileOptions = {}`, `try {`, `	ecConfigFileOptions = (await import('virtual:astro-expressive-code/ec-config')).default`, `} catch (e) {`, `	console.error('*** Failed to load Expressive Code config file ${strEcConfigFileUrlHref}. You can ignore this message if you just renamed/removed the file.\\n\\n(Full error message: "' + (e?.message || e) + '")\\n')`, `}`, `export { ecConfigFileOptions }`);
	modules["virtual:astro-expressive-code/config"] = configModuleContents.join("\n");
	modules["virtual:astro-expressive-code/ec-config"] = "export default {}";
	modules["virtual:astro-expressive-code/preprocess-config"] = customConfigPreprocessors?.preprocessComponentConfig || `export default ({ ecConfig }) => ecConfig`;
	const shikiConfig = typeof processedEcConfig.shiki === "object" ? processedEcConfig.shiki : {};
	const configuredEngine = shikiConfig.engine === "javascript" ? "javascript" : "oniguruma";
	const anyThemeOrThemes = processedEcConfig;
	const effectiveThemesOrTheme = anyThemeOrThemes.themes ?? anyThemeOrThemes.theme ?? [];
	const configuredBundledThemes = (Array.isArray(effectiveThemesOrTheme) ? effectiveThemesOrTheme : [effectiveThemesOrTheme]).filter((theme) => typeof theme === "string");
	const shikiAssetRegExp = /(?<=\n)\s*\{[\s\S]*?"id": "(.*?)",[\s\S]*?\n\s*\},?\s*\n/g;
	const shikiBundledLanguagesModuleRegExp = /\/shiki\/dist\/langs(?:-bundle-full-[^/]+)?\.m?js$/;
	const noQuery = (source) => source.split("?")[0];
	const getVirtualModuleContents = (source) => {
		if (command === "dev") for (const file of [...styles, ...scripts]) {
			const [fileName, contents] = file;
			if (noQuery(fileName) === noQuery(source)) return contents;
		}
		return source in modules ? modules[source] : void 0;
	};
	return [{
		name: "vite-plugin-astro-expressive-code",
		async resolveId(source, importer) {
			if (source === "virtual:astro-expressive-code/api") {
				const resolved = await this.resolve("astro-expressive-code", importer);
				if (resolved) return resolved;
				return await this.resolve("astro-expressive-code");
			}
			if (source === "virtual:astro-expressive-code/ec-config") {
				const resolved = await this.resolve("./ec.config.mjs");
				if (resolved) return resolved;
			}
			if (getVirtualModuleContents(source)) return `\0${source}`;
		},
		load: (id) => id?.[0] === "\0" ? getVirtualModuleContents(id.slice(1)) : void 0,
		async handleHotUpdate({ modules: modules2, server }) {
			if (!modules2 || !server) return;
			const isImportedByEcConfig = (module, depth = 0) => {
				if (!module || !module.importers || depth >= 6) return false;
				for (const importingModule of module.importers) {
					if (noQuery(module.url).endsWith("/ec.config.mjs")) return true;
					if (isImportedByEcConfig(importingModule, depth + 1)) return true;
				}
				return false;
			};
			if (modules2.some((module) => isImportedByEcConfig(module))) await server.restart();
		},
		transform: (code, id) => {
			if (id.includes("/plugin-shiki/dist/")) return code.replace(/(return \[)(?:.*?shiki\/engine\/(javascript|oniguruma).*?)(\]\[0\])/g, (match, prefix, engine, suffix) => {
				if (engine === configuredEngine) return match;
				return `${prefix}undefined${suffix}`;
			});
			if (processedEcConfig.removeUnusedThemes !== false && id.match(/\/shiki\/dist\/themes\.m?js$/)) return code.replace(shikiAssetRegExp, (match, bundledTheme) => {
				if (configuredBundledThemes.includes(bundledTheme)) return match;
				return "";
			});
			if (shikiConfig.bundledLangs && id.match(shikiBundledLanguagesModuleRegExp)) return code.replace(shikiAssetRegExp, (match, bundledLang) => {
				if (shikiConfig.bundledLangs.includes(bundledLang)) return match;
				return "";
			});
		}
	}, {
		name: "vite-plugin-astro-expressive-code-build",
		apply: "build",
		buildEnd() {
			for (const file of [...styles, ...scripts]) {
				const [fileName, source] = file;
				this.emitFile({
					type: "asset",
					fileName: noQuery(fileName.slice(1)),
					source
				});
			}
		}
	}];
}
function astroExpressiveCode(integrationOptions = {}) {
	return {
		name: "astro-expressive-code",
		hooks: { "astro:config:setup": async (args) => {
			const { command, config: astroConfig, updateConfig, logger, addWatchFile } = args;
			const ownPosition = astroConfig.integrations.findIndex((integration2) => integration2.name === "astro-expressive-code");
			const mdxPosition = astroConfig.integrations.findIndex((integration2) => integration2.name === "@astrojs/mdx");
			if (ownPosition > -1 && mdxPosition > -1 && mdxPosition < ownPosition) throw new Error(`Incorrect integration order: To allow code blocks on MDX pages to use
						astro-expressive-code, please move astroExpressiveCode() before mdx()
						in the "integrations" array of your Astro config file.`.replace(/\s+/g, " "));
			addWatchFile(getEcConfigFileUrl(astroConfig.root));
			const mergedOptions = mergeEcConfigOptions(integrationOptions, await loadEcConfigFile(astroConfig.root));
			const processedEcConfig = await mergedOptions.customConfigPreprocessors?.preprocessAstroIntegrationConfig({
				ecConfig: mergedOptions,
				astroConfig
			}) || mergedOptions;
			const { customCreateAstroRenderer } = processedEcConfig;
			delete processedEcConfig.customCreateAstroRenderer;
			delete processedEcConfig.customConfigPreprocessors;
			const { hashedStyles, hashedScripts, ...renderer } = await (customCreateAstroRenderer ?? createAstroRenderer)({
				astroConfig,
				ecConfig: processedEcConfig,
				logger
			});
			const rehypeExpressiveCodeOptions = {
				...processedEcConfig,
				customCreateRenderer: () => renderer
			};
			const vite = { plugins: [vitePluginAstroExpressiveCode({
				styles: hashedStyles,
				scripts: hashedScripts,
				ecIntegrationOptions: integrationOptions,
				processedEcConfig,
				astroConfig,
				command
			})] };
			const markdownProcessor = astroConfig.markdown?.processor;
			if (isSatteriProcessor(markdownProcessor)) {
				markdownProcessor.options.hastPlugins.push(() => satteriExpressiveCodePlugin(rehypeExpressiveCodeOptions));
				updateConfig({
					vite,
					markdown: { syntaxHighlight: false }
				});
			} else if (isUnifiedProcessor(markdownProcessor)) {
				markdownProcessor.options.rehypePlugins.push(() => rehypeExpressiveCode(rehypeExpressiveCodeOptions));
				updateConfig({
					vite,
					markdown: { syntaxHighlight: false }
				});
			} else if (markdownProcessor) {
				logger.warn(`The configured \`markdown.processor\` is not supported by Expressive Code. Expressive Code won't run on your content. Switch to \`unified()\` from \`@astrojs/markdown-remark\` or \`satteri()\` from \`@astrojs/markdown-satteri\`.`);
				updateConfig({ vite });
			} else updateConfig({
				vite,
				markdown: {
					syntaxHighlight: false,
					rehypePlugins: [[rehypeExpressiveCode, rehypeExpressiveCodeOptions]]
				}
			});
		} }
	};
}
function defineEcConfig(config) {
	return config;
}
var src_default = astroExpressiveCode;
//#endregion
//#region node_modules/@astrojs/starlight/integrations/expressive-code/themes/night-owl-dark.jsonc?raw
var night_owl_dark_default = "/**\n * Night Owl VS Code Theme - https://github.com/sdras/night-owl-vscode-theme\n *\n * MIT License\n *\n * Copyright (c) 2018 Sarah Drasner\n *\n * Permission is hereby granted, free of charge, to any person obtaining a copy\n * of this software and associated documentation files (the \"Software\"), to deal\n * in the Software without restriction, including without limitation the rights\n * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n * copies of the Software, and to permit persons to whom the Software is\n * furnished to do so, subject to the following conditions:\n *\n * The above copyright notice and this permission notice shall be included in all\n * copies or substantial portions of the Software.\n *\n * THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n * SOFTWARE.\n */\n{\n	\"name\": \"Night Owl No Italics\",\n	\"type\": \"dark\",\n	\"semanticHighlighting\": false,\n	\"colors\": {\n		\"contrastBorder\": \"#122d42\",\n		\"focusBorder\": \"#122d42\",\n		\"foreground\": \"#d6deeb\",\n		\"widget.shadow\": \"#011627\",\n		\"selection.background\": \"#4373c2\",\n		\"errorForeground\": \"#EF5350\",\n		\"button.background\": \"#7e57c2cc\",\n		\"button.foreground\": \"#ffffffcc\",\n		\"button.hoverBackground\": \"#7e57c2\",\n		\"dropdown.background\": \"#011627\",\n		\"dropdown.border\": \"#5f7e97\",\n		\"dropdown.foreground\": \"#ffffffcc\",\n		\"input.background\": \"#0b253a\",\n		\"input.border\": \"#5f7e97\",\n		\"input.foreground\": \"#ffffffcc\",\n		\"input.placeholderForeground\": \"#5f7e97\",\n		\"inputOption.activeBorder\": \"#ffffffcc\",\n		\"punctuation.definition.generic.begin.html\": \"#ef5350f2\",\n		\"inputValidation.errorBackground\": \"#AB0300F2\",\n		\"inputValidation.errorBorder\": \"#EF5350\",\n		\"inputValidation.infoBackground\": \"#00589EF2\",\n		\"inputValidation.infoBorder\": \"#64B5F6\",\n		\"inputValidation.warningBackground\": \"#675700F2\",\n		\"inputValidation.warningBorder\": \"#FFCA28\",\n		\"scrollbar.shadow\": \"#010b14\",\n		\"scrollbarSlider.activeBackground\": \"#084d8180\",\n		\"scrollbarSlider.background\": \"#084d8180\",\n		\"scrollbarSlider.hoverBackground\": \"#084d8180\",\n		\"badge.background\": \"#5f7e97\",\n		\"badge.foreground\": \"#ffffff\",\n		\"progress.background\": \"#7e57c2\",\n		\"breadcrumb.foreground\": \"#A599E9\",\n		\"breadcrumb.focusForeground\": \"#ffffff\",\n		\"breadcrumb.activeSelectionForeground\": \"#FFFFFF\",\n		\"breadcrumbPicker.background\": \"#001122\",\n		\"list.activeSelectionBackground\": \"#234d708c\",\n		\"list.activeSelectionForeground\": \"#ffffff\",\n		\"list.invalidItemForeground\": \"#975f94\",\n		\"list.dropBackground\": \"#011627\",\n		\"list.focusBackground\": \"#010d18\",\n		\"list.focusForeground\": \"#ffffff\",\n		\"list.highlightForeground\": \"#ffffff\",\n		\"list.hoverBackground\": \"#011627\",\n		\"list.hoverForeground\": \"#ffffff\",\n		\"list.inactiveSelectionBackground\": \"#0e293f\",\n		\"list.inactiveSelectionForeground\": \"#5f7e97\",\n		\"activityBar.background\": \"#011627\",\n		\"activityBar.dropBackground\": \"#5f7e97\",\n		\"activityBar.foreground\": \"#5f7e97\",\n		\"activityBar.border\": \"#011627\",\n		\"activityBarBadge.background\": \"#44596b\",\n		\"activityBarBadge.foreground\": \"#ffffff\",\n		\"sideBar.background\": \"#011627\",\n		\"sideBar.foreground\": \"#89a4bb\",\n		\"sideBar.border\": \"#011627\",\n		\"sideBarTitle.foreground\": \"#5f7e97\",\n		\"sideBarSectionHeader.background\": \"#011627\",\n		\"sideBarSectionHeader.foreground\": \"#5f7e97\",\n		\"editorGroup.emptyBackground\": \"#011627\",\n		\"editorGroup.border\": \"#011627\",\n		\"editorGroup.dropBackground\": \"#7e57c273\",\n		\"editorGroupHeader.noTabsBackground\": \"#011627\",\n		\"editorGroupHeader.tabsBackground\": \"#011627\",\n		\"editorGroupHeader.tabsBorder\": \"#262A39\",\n		\"tab.activeBackground\": \"#0b2942\",\n		\"tab.activeForeground\": \"#d2dee7\",\n		\"tab.border\": \"#272B3B\",\n		\"tab.activeBorder\": \"#262A39\",\n		\"tab.unfocusedActiveBorder\": \"#262A39\",\n		\"tab.inactiveBackground\": \"#01111d\",\n		\"tab.inactiveForeground\": \"#5f7e97\",\n		\"tab.unfocusedActiveForeground\": \"#5f7e97\",\n		\"tab.unfocusedInactiveForeground\": \"#5f7e97\",\n		\"editor.background\": \"#011627\",\n		\"editor.foreground\": \"#d6deeb\",\n		\"editorLineNumber.foreground\": \"#4b6479\",\n		\"editorLineNumber.activeForeground\": \"#C5E4FD\",\n		\"editorCursor.foreground\": \"#80a4c2\",\n		\"editor.selectionBackground\": \"#1d3b53\",\n		\"editor.selectionHighlightBackground\": \"#5f7e9779\",\n		\"editor.inactiveSelectionBackground\": \"#7e57c25a\",\n		\"editor.wordHighlightBackground\": \"#f6bbe533\",\n		\"editor.wordHighlightStrongBackground\": \"#e2a2f433\",\n		\"editor.findMatchBackground\": \"#5f7e9779\",\n		\"editor.findMatchHighlightBackground\": \"#1085bb5d\",\n		\"editor.findRangeHighlightBackground\": null,\n		\"editor.hoverHighlightBackground\": \"#7e57c25a\",\n		\"editor.lineHighlightBackground\": \"#0003\",\n		\"editor.lineHighlightBorder\": null,\n		\"editorLink.activeForeground\": null,\n		\"editor.rangeHighlightBackground\": \"#7e57c25a\",\n		\"editorWhitespace.foreground\": null,\n		\"editorIndentGuide.background\": \"#5e81ce52\",\n		\"editorIndentGuide.activeBackground\": \"#7E97AC\",\n		\"editorRuler.foreground\": \"#5e81ce52\",\n		\"editorCodeLens.foreground\": \"#5e82ceb4\",\n		\"editorBracketMatch.background\": \"#5f7e974d\",\n		\"editorBracketMatch.border\": null,\n		\"editorOverviewRuler.currentContentForeground\": \"#7e57c2\",\n		\"editorOverviewRuler.incomingContentForeground\": \"#7e57c2\",\n		\"editorOverviewRuler.commonContentForeground\": \"#7e57c2\",\n		\"editorError.foreground\": \"#EF5350\",\n		\"editorError.border\": null,\n		\"editorWarning.foreground\": \"#b39554\",\n		\"editorWarning.border\": null,\n		\"editorGutter.background\": \"#011627\",\n		\"editorGutter.modifiedBackground\": \"#e2b93d\",\n		\"editorGutter.addedBackground\": \"#9CCC65\",\n		\"editorGutter.deletedBackground\": \"#EF5350\",\n		\"diffEditor.insertedTextBackground\": \"#99b76d23\",\n		\"diffEditor.insertedTextBorder\": \"#c5e47833\",\n		\"diffEditor.removedTextBackground\": \"#ef535033\",\n		\"diffEditor.removedTextBorder\": \"#ef53504d\",\n		\"editorWidget.background\": \"#021320\",\n		\"editorWidget.border\": \"#5f7e97\",\n		\"editorSuggestWidget.background\": \"#2C3043\",\n		\"editorSuggestWidget.border\": \"#2B2F40\",\n		\"editorSuggestWidget.foreground\": \"#d6deeb\",\n		\"editorSuggestWidget.highlightForeground\": \"#ffffff\",\n		\"editorSuggestWidget.selectedBackground\": \"#5f7e97\",\n		\"editorHoverWidget.background\": \"#011627\",\n		\"editorHoverWidget.border\": \"#5f7e97\",\n		\"debugExceptionWidget.background\": \"#011627\",\n		\"debugExceptionWidget.border\": \"#5f7e97\",\n		\"editorMarkerNavigation.background\": \"#0b2942\",\n		\"editorMarkerNavigationError.background\": \"#EF5350\",\n		\"editorMarkerNavigationWarning.background\": \"#FFCA28\",\n		\"peekView.border\": \"#5f7e97\",\n		\"peekViewEditor.background\": \"#011627\",\n		\"peekViewEditor.matchHighlightBackground\": \"#7e57c25a\",\n		\"peekViewResult.background\": \"#011627\",\n		\"peekViewResult.fileForeground\": \"#5f7e97\",\n		\"peekViewResult.lineForeground\": \"#5f7e97\",\n		\"peekViewResult.matchHighlightBackground\": \"#ffffffcc\",\n		\"peekViewResult.selectionBackground\": \"#2E3250\",\n		\"peekViewResult.selectionForeground\": \"#5f7e97\",\n		\"peekViewTitle.background\": \"#011627\",\n		\"peekViewTitleDescription.foreground\": \"#697098\",\n		\"peekViewTitleLabel.foreground\": \"#5f7e97\",\n		\"merge.currentHeaderBackground\": \"#5f7e97\",\n		\"merge.currentContentBackground\": null,\n		\"merge.incomingHeaderBackground\": \"#7e57c25a\",\n		\"merge.incomingContentBackground\": null,\n		\"merge.border\": null,\n		\"panel.background\": \"#011627\",\n		\"panel.border\": \"#5f7e97\",\n		\"panelTitle.activeBorder\": \"#5f7e97\",\n		\"panelTitle.activeForeground\": \"#ffffffcc\",\n		\"panelTitle.inactiveForeground\": \"#d6deeb80\",\n		\"statusBar.background\": \"#011627\",\n		\"statusBar.foreground\": \"#5f7e97\",\n		\"statusBar.border\": \"#262A39\",\n		\"statusBar.debuggingBackground\": \"#202431\",\n		\"statusBar.debuggingForeground\": null,\n		\"statusBar.debuggingBorder\": \"#1F2330\",\n		\"statusBar.noFolderForeground\": null,\n		\"statusBar.noFolderBackground\": \"#011627\",\n		\"statusBar.noFolderBorder\": \"#25293A\",\n		\"statusBarItem.activeBackground\": \"#202431\",\n		\"statusBarItem.hoverBackground\": \"#202431\",\n		\"statusBarItem.prominentBackground\": \"#202431\",\n		\"statusBarItem.prominentHoverBackground\": \"#202431\",\n		\"titleBar.activeBackground\": \"#011627\",\n		\"titleBar.activeForeground\": \"#eeefff\",\n		\"titleBar.inactiveBackground\": \"#010e1a\",\n		\"titleBar.inactiveForeground\": null,\n		\"notifications.background\": \"#01111d\",\n		\"notifications.border\": \"#262a39\",\n		\"notificationCenter.border\": \"#262a39\",\n		\"notificationToast.border\": \"#262a39\",\n		\"notifications.foreground\": \"#ffffffcc\",\n		\"notificationLink.foreground\": \"#80CBC4\",\n		\"extensionButton.prominentForeground\": \"#ffffffcc\",\n		\"extensionButton.prominentBackground\": \"#7e57c2cc\",\n		\"extensionButton.prominentHoverBackground\": \"#7e57c2\",\n		\"pickerGroup.foreground\": \"#d1aaff\",\n		\"pickerGroup.border\": \"#011627\",\n		\"terminal.ansiWhite\": \"#ffffff\",\n		\"terminal.ansiBlack\": \"#011627\",\n		\"terminal.ansiBlue\": \"#82AAFF\",\n		\"terminal.ansiCyan\": \"#21c7a8\",\n		\"terminal.ansiGreen\": \"#22da6e\",\n		\"terminal.ansiMagenta\": \"#C792EA\",\n		\"terminal.ansiRed\": \"#EF5350\",\n		\"terminal.ansiYellow\": \"#c5e478\",\n		\"terminal.ansiBrightWhite\": \"#ffffff\",\n		\"terminal.ansiBrightBlack\": \"#575656\",\n		\"terminal.ansiBrightBlue\": \"#82AAFF\",\n		\"terminal.ansiBrightCyan\": \"#7fdbca\",\n		\"terminal.ansiBrightGreen\": \"#22da6e\",\n		\"terminal.ansiBrightMagenta\": \"#C792EA\",\n		\"terminal.ansiBrightRed\": \"#EF5350\",\n		\"terminal.ansiBrightYellow\": \"#ffeb95\",\n		\"terminal.selectionBackground\": \"#1b90dd4d\",\n		\"terminalCursor.background\": \"#234d70\",\n		\"textCodeBlock.background\": \"#4f4f4f\",\n		\"debugToolBar.background\": \"#011627\",\n		\"welcomePage.buttonBackground\": \"#011627\",\n		\"welcomePage.buttonHoverBackground\": \"#011627\",\n		\"walkThrough.embeddedEditorBackground\": \"#011627\",\n		\"gitDecoration.modifiedResourceForeground\": \"#a2bffc\",\n		\"gitDecoration.deletedResourceForeground\": \"#EF535090\",\n		\"gitDecoration.untrackedResourceForeground\": \"#c5e478ff\",\n		\"gitDecoration.ignoredResourceForeground\": \"#395a75\",\n		\"gitDecoration.conflictingResourceForeground\": \"#ffeb95cc\",\n		\"source.elm\": \"#5f7e97\",\n		\"string.quoted.single.js\": \"#ffffff\",\n		\"meta.objectliteral.js\": \"#82AAFF\",\n	},\n	\"tokenColors\": [\n		{\n			\"name\": \"Changed\",\n			\"scope\": [\n				\"markup.changed\",\n				\"meta.diff.header.git\",\n				\"meta.diff.header.from-file\",\n				\"meta.diff.header.to-file\",\n			],\n			\"settings\": {\n				\"foreground\": \"#a2bffc\",\n			},\n		},\n		{\n			\"name\": \"Deleted\",\n			\"scope\": \"markup.deleted.diff\",\n			\"settings\": {\n				\"foreground\": \"#EF535090\",\n			},\n		},\n		{\n			\"name\": \"Inserted\",\n			\"scope\": \"markup.inserted.diff\",\n			\"settings\": {\n				\"foreground\": \"#c5e478ff\",\n			},\n		},\n		{\n			\"name\": \"Global settings\",\n			\"settings\": {\n				\"background\": \"#011627\",\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"Comment\",\n			\"scope\": \"comment\",\n			\"settings\": {\n				\"foreground\": \"#637777\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"String\",\n			\"scope\": \"string\",\n			\"settings\": {\n				\"foreground\": \"#ecc48d\",\n			},\n		},\n		{\n			\"name\": \"String Quoted\",\n			\"scope\": [\"string.quoted\", \"variable.other.readwrite.js\"],\n			\"settings\": {\n				\"foreground\": \"#ecc48d\",\n			},\n		},\n		{\n			\"name\": \"Support Constant Math\",\n			\"scope\": \"support.constant.math\",\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"Number\",\n			\"scope\": [\"constant.numeric\", \"constant.character.numeric\"],\n			\"settings\": {\n				\"foreground\": \"#F78C6C\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Built-in constant\",\n			\"scope\": [\"constant.language\", \"punctuation.definition.constant\", \"variable.other.constant\"],\n			\"settings\": {\n				\"foreground\": \"#82AAFF\",\n			},\n		},\n		{\n			\"name\": \"User-defined constant\",\n			\"scope\": [\"constant.character\", \"constant.other\"],\n			\"settings\": {\n				\"foreground\": \"#82AAFF\",\n			},\n		},\n		{\n			\"name\": \"Constant Character Escape\",\n			\"scope\": \"constant.character.escape\",\n			\"settings\": {\n				\"foreground\": \"#F78C6C\",\n			},\n		},\n		{\n			\"name\": \"RegExp String\",\n			\"scope\": [\"string.regexp\", \"string.regexp keyword.other\"],\n			\"settings\": {\n				\"foreground\": \"#5ca7e4\",\n			},\n		},\n		{\n			\"name\": \"Comma in functions\",\n			\"scope\": \"meta.function punctuation.separator.comma\",\n			\"settings\": {\n				\"foreground\": \"#5f7e97\",\n			},\n		},\n		{\n			\"name\": \"Variable\",\n			\"scope\": \"variable\",\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"Keyword\",\n			\"scope\": [\"punctuation.accessor\", \"keyword\"],\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Storage\",\n			\"scope\": [\n				\"storage\",\n				\"meta.var.expr\",\n				\"meta.class meta.method.declaration meta.var.expr storage.type.js\",\n				\"storage.type.property.js\",\n				\"storage.type.property.ts\",\n				\"storage.type.property.tsx\",\n			],\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Storage type\",\n			\"scope\": \"storage.type\",\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n			},\n		},\n		{\n			\"name\": \"Storage type\",\n			\"scope\": \"storage.type.function.arrow.js\",\n			\"settings\": {\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Class name\",\n			\"scope\": [\"entity.name.class\", \"meta.class entity.name.type.class\"],\n			\"settings\": {\n				\"foreground\": \"#ffcb8b\",\n			},\n		},\n		{\n			\"name\": \"Inherited class\",\n			\"scope\": \"entity.other.inherited-class\",\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"Function name\",\n			\"scope\": \"entity.name.function\",\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Meta Tag\",\n			\"scope\": [\"punctuation.definition.tag\", \"meta.tag\"],\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n			},\n		},\n		{\n			\"name\": \"HTML Tag names\",\n			\"scope\": [\n				\"entity.name.tag\",\n				\"meta.tag.other.html\",\n				\"meta.tag.other.js\",\n				\"meta.tag.other.tsx\",\n				\"entity.name.tag.tsx\",\n				\"entity.name.tag.js\",\n				\"entity.name.tag\",\n				\"meta.tag.js\",\n				\"meta.tag.tsx\",\n				\"meta.tag.html\",\n			],\n			\"settings\": {\n				\"foreground\": \"#caece6\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Tag attribute\",\n			\"scope\": \"entity.other.attribute-name\",\n			\"settings\": {\n				\"fontStyle\": \"\",\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"Entity Name Tag Custom\",\n			\"scope\": \"entity.name.tag.custom\",\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"Library (function & constant)\",\n			\"scope\": [\"support.function\", \"support.constant\"],\n			\"settings\": {\n				\"foreground\": \"#82AAFF\",\n			},\n		},\n		{\n			\"name\": \"Support Constant Property Value meta\",\n			\"scope\": \"support.constant.meta.property-value\",\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n			},\n		},\n		{\n			\"name\": \"Library class/type\",\n			\"scope\": [\"support.type\", \"support.class\"],\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"Support Variable DOM\",\n			\"scope\": \"support.variable.dom\",\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"Invalid\",\n			\"scope\": \"invalid\",\n			\"settings\": {\n				\"background\": \"#ff2c83\",\n				\"foreground\": \"#ffffff\",\n			},\n		},\n		{\n			\"name\": \"Invalid deprecated\",\n			\"scope\": \"invalid.deprecated\",\n			\"settings\": {\n				\"foreground\": \"#ffffff\",\n				\"background\": \"#d3423e\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator\",\n			\"scope\": \"keyword.operator\",\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator Relational\",\n			\"scope\": \"keyword.operator.relational\",\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator Assignment\",\n			\"scope\": \"keyword.operator.assignment\",\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator Arithmetic\",\n			\"scope\": \"keyword.operator.arithmetic\",\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator Bitwise\",\n			\"scope\": \"keyword.operator.bitwise\",\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator Increment\",\n			\"scope\": \"keyword.operator.increment\",\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator Ternary\",\n			\"scope\": \"keyword.operator.ternary\",\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n			},\n		},\n		{\n			\"name\": \"Double-Slashed Comment\",\n			\"scope\": \"comment.line.double-slash\",\n			\"settings\": {\n				\"foreground\": \"#637777\",\n			},\n		},\n		{\n			\"name\": \"Object\",\n			\"scope\": \"object\",\n			\"settings\": {\n				\"foreground\": \"#cdebf7\",\n			},\n		},\n		{\n			\"name\": \"Null\",\n			\"scope\": \"constant.language.null\",\n			\"settings\": {\n				\"foreground\": \"#ff5874\",\n			},\n		},\n		{\n			\"name\": \"Meta Brace\",\n			\"scope\": \"meta.brace\",\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"Meta Delimiter Period\",\n			\"scope\": \"meta.delimiter.period\",\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Punctuation Definition String\",\n			\"scope\": \"punctuation.definition.string\",\n			\"settings\": {\n				\"foreground\": \"#d9f5dd\",\n			},\n		},\n		{\n			\"name\": \"Punctuation Definition String Markdown\",\n			\"scope\": \"punctuation.definition.string.begin.markdown\",\n			\"settings\": {\n				\"foreground\": \"#ff5874\",\n			},\n		},\n		{\n			\"name\": \"Boolean\",\n			\"scope\": \"constant.language.boolean\",\n			\"settings\": {\n				\"foreground\": \"#ff5874\",\n			},\n		},\n		{\n			\"name\": \"Object Comma\",\n			\"scope\": \"object.comma\",\n			\"settings\": {\n				\"foreground\": \"#ffffff\",\n			},\n		},\n		{\n			\"name\": \"Variable Parameter Function\",\n			\"scope\": \"variable.parameter.function\",\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Support Type Property Name & entity name tags\",\n			\"scope\": [\n				\"support.type.vendor.property-name\",\n				\"support.constant.vendor.property-value\",\n				\"support.type.property-name\",\n				\"meta.property-list entity.name.tag\",\n			],\n			\"settings\": {\n				\"foreground\": \"#80CBC4\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Entity Name tag reference in stylesheets\",\n			\"scope\": \"meta.property-list entity.name.tag.reference\",\n			\"settings\": {\n				\"foreground\": \"#57eaf1\",\n			},\n		},\n		{\n			\"name\": \"Constant Other Color RGB Value Punctuation Definition Constant\",\n			\"scope\": \"constant.other.color.rgb-value punctuation.definition.constant\",\n			\"settings\": {\n				\"foreground\": \"#F78C6C\",\n			},\n		},\n		{\n			\"name\": \"Constant Other Color\",\n			\"scope\": \"constant.other.color\",\n			\"settings\": {\n				\"foreground\": \"#FFEB95\",\n			},\n		},\n		{\n			\"name\": \"Keyword Other Unit\",\n			\"scope\": \"keyword.other.unit\",\n			\"settings\": {\n				\"foreground\": \"#FFEB95\",\n			},\n		},\n		{\n			\"name\": \"Meta Selector\",\n			\"scope\": \"meta.selector\",\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Entity Other Attribute Name Id\",\n			\"scope\": \"entity.other.attribute-name.id\",\n			\"settings\": {\n				\"foreground\": \"#FAD430\",\n			},\n		},\n		{\n			\"name\": \"Meta Property Name\",\n			\"scope\": \"meta.property-name\",\n			\"settings\": {\n				\"foreground\": \"#80CBC4\",\n			},\n		},\n		{\n			\"name\": \"Doctypes\",\n			\"scope\": [\"entity.name.tag.doctype\", \"meta.tag.sgml.doctype\"],\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Punctuation Definition Parameters\",\n			\"scope\": \"punctuation.definition.parameters\",\n			\"settings\": {\n				\"foreground\": \"#d9f5dd\",\n			},\n		},\n		{\n			\"name\": \"Keyword Control Operator\",\n			\"scope\": \"keyword.control.operator\",\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator Logical\",\n			\"scope\": \"keyword.operator.logical\",\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Variable Instances\",\n			\"scope\": [\n				\"variable.instance\",\n				\"variable.other.instance\",\n				\"variable.readwrite.instance\",\n				\"variable.other.readwrite.instance\",\n				\"variable.other.property\",\n			],\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n			},\n		},\n		{\n			\"name\": \"Variable Property Other object property\",\n			\"scope\": [\"variable.other.object.property\"],\n			\"settings\": {\n				\"foreground\": \"#faf39f\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Variable Property Other object\",\n			\"scope\": [\"variable.other.object.js\"],\n			\"settings\": {\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Entity Name Function\",\n			\"scope\": [\"entity.name.function\"],\n			\"settings\": {\n				\"foreground\": \"#82AAFF\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator Comparison, returns, imports, and Keyword Operator Ruby\",\n			\"scope\": [\n				\"keyword.control.conditional.js\",\n				\"keyword.operator.comparison\",\n				\"keyword.control.flow.js\",\n				\"keyword.control.flow.ts\",\n				\"keyword.control.flow.tsx\",\n				\"keyword.control.ruby\",\n				\"keyword.control.def.ruby\",\n				\"keyword.control.loop.js\",\n				\"keyword.control.loop.ts\",\n				\"keyword.control.import.js\",\n				\"keyword.control.import.ts\",\n				\"keyword.control.import.tsx\",\n				\"keyword.control.from.js\",\n				\"keyword.control.from.ts\",\n				\"keyword.control.from.tsx\",\n				\"keyword.control.conditional.js\",\n				\"keyword.control.conditional.ts\",\n				\"keyword.control.switch.js\",\n				\"keyword.control.switch.ts\",\n				\"keyword.operator.instanceof.js\",\n				\"keyword.operator.expression.instanceof.ts\",\n				\"keyword.operator.expression.instanceof.tsx\",\n			],\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Support Constant, `new` keyword, Special Method Keyword, `debugger`, other keywords\",\n			\"scope\": [\n				\"support.constant\",\n				\"keyword.other.special-method\",\n				\"keyword.other.new\",\n				\"keyword.other.debugger\",\n				\"keyword.control\",\n			],\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n			},\n		},\n		{\n			\"name\": \"Support Function\",\n			\"scope\": \"support.function\",\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"Invalid Broken\",\n			\"scope\": \"invalid.broken\",\n			\"settings\": {\n				\"foreground\": \"#020e14\",\n				\"background\": \"#F78C6C\",\n			},\n		},\n		{\n			\"name\": \"Invalid Unimplemented\",\n			\"scope\": \"invalid.unimplemented\",\n			\"settings\": {\n				\"background\": \"#8BD649\",\n				\"foreground\": \"#ffffff\",\n			},\n		},\n		{\n			\"name\": \"Invalid Illegal\",\n			\"scope\": \"invalid.illegal\",\n			\"settings\": {\n				\"foreground\": \"#ffffff\",\n				\"background\": \"#ec5f67\",\n			},\n		},\n		{\n			\"name\": \"Language Variable\",\n			\"scope\": \"variable.language\",\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n			},\n		},\n		{\n			\"name\": \"Support Variable Property\",\n			\"scope\": \"support.variable.property\",\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n			},\n		},\n		{\n			\"name\": \"Variable Function\",\n			\"scope\": \"variable.function\",\n			\"settings\": {\n				\"foreground\": \"#82AAFF\",\n			},\n		},\n		{\n			\"name\": \"Variable Interpolation\",\n			\"scope\": \"variable.interpolation\",\n			\"settings\": {\n				\"foreground\": \"#ec5f67\",\n			},\n		},\n		{\n			\"name\": \"Meta Function Call\",\n			\"scope\": \"meta.function-call\",\n			\"settings\": {\n				\"foreground\": \"#82AAFF\",\n			},\n		},\n		{\n			\"name\": \"Punctuation Section Embedded\",\n			\"scope\": \"punctuation.section.embedded\",\n			\"settings\": {\n				\"foreground\": \"#d3423e\",\n			},\n		},\n		{\n			\"name\": \"Punctuation Tweaks\",\n			\"scope\": [\n				\"punctuation.terminator.expression\",\n				\"punctuation.definition.arguments\",\n				\"punctuation.definition.array\",\n				\"punctuation.section.array\",\n				\"meta.array\",\n			],\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"More Punctuation Tweaks\",\n			\"scope\": [\n				\"punctuation.definition.list.begin\",\n				\"punctuation.definition.list.end\",\n				\"punctuation.separator.arguments\",\n				\"punctuation.definition.list\",\n			],\n			\"settings\": {\n				\"foreground\": \"#d9f5dd\",\n			},\n		},\n		{\n			\"name\": \"Template Strings\",\n			\"scope\": \"string.template meta.template.expression\",\n			\"settings\": {\n				\"foreground\": \"#d3423e\",\n			},\n		},\n		{\n			\"name\": \"Backticks(``) in Template Strings\",\n			\"scope\": \"string.template punctuation.definition.string\",\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"Italics\",\n			\"scope\": \"italic\",\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n				\"fontStyle\": \"italic\",\n			},\n		},\n		{\n			\"name\": \"Bold\",\n			\"scope\": \"bold\",\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n				\"fontStyle\": \"bold\",\n			},\n		},\n		{\n			\"name\": \"Quote\",\n			\"scope\": \"quote\",\n			\"settings\": {\n				\"foreground\": \"#697098\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Raw Code\",\n			\"scope\": \"raw\",\n			\"settings\": {\n				\"foreground\": \"#80CBC4\",\n			},\n		},\n		{\n			\"name\": \"CoffeeScript Variable Assignment\",\n			\"scope\": \"variable.assignment.coffee\",\n			\"settings\": {\n				\"foreground\": \"#31e1eb\",\n			},\n		},\n		{\n			\"name\": \"CoffeeScript Parameter Function\",\n			\"scope\": \"variable.parameter.function.coffee\",\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"CoffeeScript Assignments\",\n			\"scope\": \"variable.assignment.coffee\",\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n			},\n		},\n		{\n			\"name\": \"C# Readwrite Variables\",\n			\"scope\": \"variable.other.readwrite.cs\",\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"C# Classes & Storage types\",\n			\"scope\": [\"entity.name.type.class.cs\", \"storage.type.cs\"],\n			\"settings\": {\n				\"foreground\": \"#ffcb8b\",\n			},\n		},\n		{\n			\"name\": \"C# Namespaces\",\n			\"scope\": \"entity.name.type.namespace.cs\",\n			\"settings\": {\n				\"foreground\": \"#B2CCD6\",\n			},\n		},\n		{\n			\"name\": \"C# Unquoted String Zone\",\n			\"scope\": \"string.unquoted.preprocessor.message.cs\",\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"C# Region\",\n			\"scope\": [\n				\"punctuation.separator.hash.cs\",\n				\"keyword.preprocessor.region.cs\",\n				\"keyword.preprocessor.endregion.cs\",\n			],\n			\"settings\": {\n				\"foreground\": \"#ffcb8b\",\n				\"fontStyle\": \"bold\",\n			},\n		},\n		{\n			\"name\": \"C# Other Variables\",\n			\"scope\": \"variable.other.object.cs\",\n			\"settings\": {\n				\"foreground\": \"#B2CCD6\",\n			},\n		},\n		{\n			\"name\": \"C# Enum\",\n			\"scope\": \"entity.name.type.enum.cs\",\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"Dart String\",\n			\"scope\": [\"string.interpolated.single.dart\", \"string.interpolated.double.dart\"],\n			\"settings\": {\n				\"foreground\": \"#FFCB8B\",\n			},\n		},\n		{\n			\"name\": \"Dart Class\",\n			\"scope\": \"support.class.dart\",\n			\"settings\": {\n				\"foreground\": \"#FFCB8B\",\n			},\n		},\n		{\n			\"name\": \"Tag names in Stylesheets\",\n			\"scope\": [\n				\"entity.name.tag.css\",\n				\"entity.name.tag.less\",\n				\"entity.name.tag.custom.css\",\n				\"support.constant.property-value.css\",\n			],\n			\"settings\": {\n				\"foreground\": \"#ff6363\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Wildcard(*) selector in Stylesheets\",\n			\"scope\": [\n				\"entity.name.tag.wildcard.css\",\n				\"entity.name.tag.wildcard.less\",\n				\"entity.name.tag.wildcard.scss\",\n				\"entity.name.tag.wildcard.sass\",\n			],\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n			},\n		},\n		{\n			\"name\": \"CSS Keyword Other Unit\",\n			\"scope\": \"keyword.other.unit.css\",\n			\"settings\": {\n				\"foreground\": \"#FFEB95\",\n			},\n		},\n		{\n			\"name\": \"Attribute Name for CSS\",\n			\"scope\": [\n				\"meta.attribute-selector.css entity.other.attribute-name.attribute\",\n				\"variable.other.readwrite.js\",\n			],\n			\"settings\": {\n				\"foreground\": \"#F78C6C\",\n			},\n		},\n		{\n			\"name\": \"Elixir Classes\",\n			\"scope\": [\n				\"source.elixir support.type.elixir\",\n				\"source.elixir meta.module.elixir entity.name.class.elixir\",\n			],\n			\"settings\": {\n				\"foreground\": \"#82AAFF\",\n			},\n		},\n		{\n			\"name\": \"Elixir Functions\",\n			\"scope\": \"source.elixir entity.name.function\",\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"Elixir Constants\",\n			\"scope\": [\n				\"source.elixir constant.other.symbol.elixir\",\n				\"source.elixir constant.other.keywords.elixir\",\n			],\n			\"settings\": {\n				\"foreground\": \"#82AAFF\",\n			},\n		},\n		{\n			\"name\": \"Elixir String Punctuations\",\n			\"scope\": \"source.elixir punctuation.definition.string\",\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"Elixir\",\n			\"scope\": [\n				\"source.elixir variable.other.readwrite.module.elixir\",\n				\"source.elixir variable.other.readwrite.module.elixir punctuation.definition.variable.elixir\",\n			],\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"Elixir Binary Punctuations\",\n			\"scope\": \"source.elixir .punctuation.binary.elixir\",\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Closure Constant Keyword\",\n			\"scope\": \"constant.keyword.clojure\",\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n			},\n		},\n		{\n			\"name\": \"Go Function Calls\",\n			\"scope\": \"source.go meta.function-call.go\",\n			\"settings\": {\n				\"foreground\": \"#DDDDDD\",\n			},\n		},\n		{\n			\"name\": \"Go Keywords\",\n			\"scope\": [\n				\"source.go keyword.package.go\",\n				\"source.go keyword.import.go\",\n				\"source.go keyword.function.go\",\n				\"source.go keyword.type.go\",\n				\"source.go keyword.struct.go\",\n				\"source.go keyword.interface.go\",\n				\"source.go keyword.const.go\",\n				\"source.go keyword.var.go\",\n				\"source.go keyword.map.go\",\n				\"source.go keyword.channel.go\",\n				\"source.go keyword.control.go\",\n			],\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n			},\n		},\n		{\n			\"name\": \"Go Constants e.g. nil, string format (%s, %d, etc.)\",\n			\"scope\": [\"source.go constant.language.go\", \"source.go constant.other.placeholder.go\"],\n			\"settings\": {\n				\"foreground\": \"#ff5874\",\n			},\n		},\n		{\n			\"name\": \"C++ Functions\",\n			\"scope\": [\"entity.name.function.preprocessor.cpp\", \"entity.scope.name.cpp\"],\n			\"settings\": {\n				\"foreground\": \"#7fdbcaff\",\n			},\n		},\n		{\n			\"name\": \"C++ Meta Namespace\",\n			\"scope\": [\"meta.namespace-block.cpp\"],\n			\"settings\": {\n				\"foreground\": \"#e0dec6\",\n			},\n		},\n		{\n			\"name\": \"C++ Language Primitive Storage\",\n			\"scope\": [\"storage.type.language.primitive.cpp\"],\n			\"settings\": {\n				\"foreground\": \"#ff5874\",\n			},\n		},\n		{\n			\"name\": \"C++ Preprocessor Macro\",\n			\"scope\": [\"meta.preprocessor.macro.cpp\"],\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"C++ Variable Parameter\",\n			\"scope\": [\"variable.parameter\"],\n			\"settings\": {\n				\"foreground\": \"#ffcb8b\",\n			},\n		},\n		{\n			\"name\": \"Powershell Variables\",\n			\"scope\": [\"variable.other.readwrite.powershell\"],\n			\"settings\": {\n				\"foreground\": \"#82AAFF\",\n			},\n		},\n		{\n			\"name\": \"Powershell Function\",\n			\"scope\": [\"support.function.powershell\"],\n			\"settings\": {\n				\"foreground\": \"#7fdbcaff\",\n			},\n		},\n		{\n			\"name\": \"ID Attribute Name in HTML\",\n			\"scope\": \"entity.other.attribute-name.id.html\",\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"HTML Punctuation Definition Tag\",\n			\"scope\": \"punctuation.definition.tag.html\",\n			\"settings\": {\n				\"foreground\": \"#6ae9f0\",\n			},\n		},\n		{\n			\"name\": \"HTML Doctype\",\n			\"scope\": \"meta.tag.sgml.doctype.html\",\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Classes\",\n			\"scope\": \"meta.class entity.name.type.class.js\",\n			\"settings\": {\n				\"foreground\": \"#ffcb8b\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Method Declaration e.g. `constructor`\",\n			\"scope\": \"meta.method.declaration storage.type.js\",\n			\"settings\": {\n				\"foreground\": \"#82AAFF\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Terminator\",\n			\"scope\": \"terminator.js\",\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Meta Punctuation Definition\",\n			\"scope\": \"meta.js punctuation.definition.js\",\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"Entity Names in Code Documentations\",\n			\"scope\": [\"entity.name.type.instance.jsdoc\", \"entity.name.type.instance.phpdoc\"],\n			\"settings\": {\n				\"foreground\": \"#5f7e97\",\n			},\n		},\n		{\n			\"name\": \"Other Variables in Code Documentations\",\n			\"scope\": [\"variable.other.jsdoc\", \"variable.other.phpdoc\"],\n			\"settings\": {\n				\"foreground\": \"#78ccf0\",\n			},\n		},\n		{\n			\"name\": \"JavaScript module imports and exports\",\n			\"scope\": [\n				\"variable.other.meta.import.js\",\n				\"meta.import.js variable.other\",\n				\"variable.other.meta.export.js\",\n				\"meta.export.js variable.other\",\n			],\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Variable Parameter Function\",\n			\"scope\": \"variable.parameter.function.js\",\n			\"settings\": {\n				\"foreground\": \"#7986E7\",\n			},\n		},\n		{\n			\"name\": \"JavaScript[React] Variable Other Object\",\n			\"scope\": [\n				\"variable.other.object.js\",\n				\"variable.other.object.jsx\",\n				\"variable.object.property.js\",\n				\"variable.object.property.jsx\",\n			],\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Variables\",\n			\"scope\": [\"variable.js\", \"variable.other.js\"],\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Entity Name Type\",\n			\"scope\": [\"entity.name.type.js\", \"entity.name.type.module.js\"],\n			\"settings\": {\n				\"foreground\": \"#ffcb8b\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Support Classes\",\n			\"scope\": \"support.class.js\",\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"JSON Property Names\",\n			\"scope\": \"support.type.property-name.json\",\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n			},\n		},\n		{\n			\"name\": \"JSON Support Constants\",\n			\"scope\": \"support.constant.json\",\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"JSON Property values (string)\",\n			\"scope\": \"meta.structure.dictionary.value.json string.quoted.double\",\n			\"settings\": {\n				\"foreground\": \"#c789d6\",\n			},\n		},\n		{\n			\"name\": \"Strings in JSON values\",\n			\"scope\": \"string.quoted.double.json punctuation.definition.string.json\",\n			\"settings\": {\n				\"foreground\": \"#80CBC4\",\n			},\n		},\n		{\n			\"name\": \"Specific JSON Property values like null\",\n			\"scope\": \"meta.structure.dictionary.json meta.structure.dictionary.value constant.language\",\n			\"settings\": {\n				\"foreground\": \"#ff5874\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Other Variable\",\n			\"scope\": \"variable.other.object.js\",\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n			},\n		},\n		{\n			\"name\": \"Ruby Variables\",\n			\"scope\": [\"variable.other.ruby\"],\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"Ruby Class\",\n			\"scope\": [\"entity.name.type.class.ruby\"],\n			\"settings\": {\n				\"foreground\": \"#ecc48d\",\n			},\n		},\n		{\n			\"name\": \"Ruby Hashkeys\",\n			\"scope\": \"constant.language.symbol.hashkey.ruby\",\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n			},\n		},\n		{\n			\"name\": \"LESS Tag names\",\n			\"scope\": \"entity.name.tag.less\",\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n			},\n		},\n		{\n			\"name\": \"LESS Keyword Other Unit\",\n			\"scope\": \"keyword.other.unit.css\",\n			\"settings\": {\n				\"foreground\": \"#FFEB95\",\n			},\n		},\n		{\n			\"name\": \"Attribute Name for LESS\",\n			\"scope\": \"meta.attribute-selector.less entity.other.attribute-name.attribute\",\n			\"settings\": {\n				\"foreground\": \"#F78C6C\",\n			},\n		},\n		{\n			\"name\": \"Markdown Headings\",\n			\"scope\": [\n				\"markup.heading.markdown\",\n				\"markup.heading.setext.1.markdown\",\n				\"markup.heading.setext.2.markdown\",\n			],\n			\"settings\": {\n				\"foreground\": \"#82b1ff\",\n			},\n		},\n		{\n			\"name\": \"Markdown Italics\",\n			\"scope\": \"markup.italic.markdown\",\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n				\"fontStyle\": \"italic\",\n			},\n		},\n		{\n			\"name\": \"Markdown Bold\",\n			\"scope\": \"markup.bold.markdown\",\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n				\"fontStyle\": \"bold\",\n			},\n		},\n		{\n			\"name\": \"Markdown Quote + others\",\n			\"scope\": \"markup.quote.markdown\",\n			\"settings\": {\n				\"foreground\": \"#697098\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Markdown Raw Code + others\",\n			\"scope\": \"markup.inline.raw.markdown\",\n			\"settings\": {\n				\"foreground\": \"#80CBC4\",\n			},\n		},\n		{\n			\"name\": \"Markdown Links\",\n			\"scope\": [\"markup.underline.link.markdown\", \"markup.underline.link.image.markdown\"],\n			\"settings\": {\n				\"foreground\": \"#ff869a\",\n			},\n		},\n		{\n			\"name\": \"Markdown Link Title and Description\",\n			\"scope\": [\"string.other.link.title.markdown\", \"string.other.link.description.markdown\"],\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"Markdown Punctuation\",\n			\"scope\": [\n				\"punctuation.definition.string.markdown\",\n				\"punctuation.definition.string.begin.markdown\",\n				\"punctuation.definition.string.end.markdown\",\n				\"meta.link.inline.markdown punctuation.definition.string\",\n			],\n			\"settings\": {\n				\"foreground\": \"#82b1ff\",\n			},\n		},\n		{\n			\"name\": \"Markdown MetaData Punctuation\",\n			\"scope\": [\"punctuation.definition.metadata.markdown\"],\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n			},\n		},\n		{\n			\"name\": \"Markdown List Punctuation\",\n			\"scope\": [\"beginning.punctuation.definition.list.markdown\"],\n			\"settings\": {\n				\"foreground\": \"#82b1ff\",\n			},\n		},\n		{\n			\"name\": \"Markdown Inline Raw String\",\n			\"scope\": \"markup.inline.raw.string.markdown\",\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"PHP Variables\",\n			\"scope\": \"variable.other.php\",\n			\"settings\": {\n				\"foreground\": \"#bec5d4\",\n			},\n		},\n		{\n			\"name\": \"Support Classes in PHP\",\n			\"scope\": \"support.class.php\",\n			\"settings\": {\n				\"foreground\": \"#ffcb8b\",\n			},\n		},\n		{\n			\"name\": \"Punctuations in PHP function calls\",\n			\"scope\": \"meta.function-call.php punctuation\",\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"PHP Global Variables\",\n			\"scope\": \"variable.other.global.php\",\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"Declaration Punctuation in PHP Global Variables\",\n			\"scope\": \"variable.other.global.php punctuation.definition.variable\",\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"Language Constants in Python\",\n			\"scope\": \"constant.language.python\",\n			\"settings\": {\n				\"foreground\": \"#ff5874\",\n			},\n		},\n		{\n			\"name\": \"Python Function Parameter and Arguments\",\n			\"scope\": [\"variable.parameter.function.python\", \"meta.function-call.arguments.python\"],\n			\"settings\": {\n				\"foreground\": \"#82AAFF\",\n			},\n		},\n		{\n			\"name\": \"Python Function Call\",\n			\"scope\": [\"meta.function-call.python\", \"meta.function-call.generic.python\"],\n			\"settings\": {\n				\"foreground\": \"#B2CCD6\",\n			},\n		},\n		{\n			\"name\": \"Punctuations in Python\",\n			\"scope\": \"punctuation.python\",\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"Decorator Functions in Python\",\n			\"scope\": \"entity.name.function.decorator.python\",\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"Python Language Variable\",\n			\"scope\": \"source.python variable.language.special\",\n			\"settings\": {\n				\"foreground\": \"#8EACE3\",\n			},\n		},\n		{\n			\"name\": \"Python import control keyword\",\n			\"scope\": \"keyword.control\",\n			\"settings\": {\n				\"foreground\": \"#c792ea\",\n			},\n		},\n		{\n			\"name\": \"SCSS Variable\",\n			\"scope\": [\n				\"variable.scss\",\n				\"variable.sass\",\n				\"variable.parameter.url.scss\",\n				\"variable.parameter.url.sass\",\n			],\n			\"settings\": {\n				\"foreground\": \"#c5e478\",\n			},\n		},\n		{\n			\"name\": \"Variables in SASS At-Rules\",\n			\"scope\": [\"source.css.scss meta.at-rule variable\", \"source.css.sass meta.at-rule variable\"],\n			\"settings\": {\n				\"foreground\": \"#82AAFF\",\n			},\n		},\n		{\n			\"name\": \"Variables in SASS At-Rules\",\n			\"scope\": [\"source.css.scss meta.at-rule variable\", \"source.css.sass meta.at-rule variable\"],\n			\"settings\": {\n				\"foreground\": \"#bec5d4\",\n			},\n		},\n		{\n			\"name\": \"Attribute Name for SASS\",\n			\"scope\": [\n				\"meta.attribute-selector.scss entity.other.attribute-name.attribute\",\n				\"meta.attribute-selector.sass entity.other.attribute-name.attribute\",\n			],\n			\"settings\": {\n				\"foreground\": \"#F78C6C\",\n			},\n		},\n		{\n			\"name\": \"Tag names in SASS\",\n			\"scope\": [\"entity.name.tag.scss\", \"entity.name.tag.sass\"],\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n			},\n		},\n		{\n			\"name\": \"SASS Keyword Other Unit\",\n			\"scope\": [\"keyword.other.unit.scss\", \"keyword.other.unit.sass\"],\n			\"settings\": {\n				\"foreground\": \"#FFEB95\",\n			},\n		},\n		{\n			\"name\": \"TypeScript[React] Variables and Object Properties\",\n			\"scope\": [\n				\"variable.other.readwrite.alias.ts\",\n				\"variable.other.readwrite.alias.tsx\",\n				\"variable.other.readwrite.ts\",\n				\"variable.other.readwrite.tsx\",\n				\"variable.other.object.ts\",\n				\"variable.other.object.tsx\",\n				\"variable.object.property.ts\",\n				\"variable.object.property.tsx\",\n				\"variable.other.ts\",\n				\"variable.other.tsx\",\n				\"variable.tsx\",\n				\"variable.ts\",\n			],\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"TypeScript[React] Entity Name Types\",\n			\"scope\": [\"entity.name.type.ts\", \"entity.name.type.tsx\"],\n			\"settings\": {\n				\"foreground\": \"#ffcb8b\",\n			},\n		},\n		{\n			\"name\": \"TypeScript[React] Node Classes\",\n			\"scope\": [\"support.class.node.ts\", \"support.class.node.tsx\"],\n			\"settings\": {\n				\"foreground\": \"#82AAFF\",\n			},\n		},\n		{\n			\"name\": \"TypeScript[React] Entity Name Types as Parameters\",\n			\"scope\": [\n				\"meta.type.parameters.ts entity.name.type\",\n				\"meta.type.parameters.tsx entity.name.type\",\n			],\n			\"settings\": {\n				\"foreground\": \"#5f7e97\",\n			},\n		},\n		{\n			\"name\": \"TypeScript[React] Import/Export Punctuations\",\n			\"scope\": [\n				\"meta.import.ts punctuation.definition.block\",\n				\"meta.import.tsx punctuation.definition.block\",\n				\"meta.export.ts punctuation.definition.block\",\n				\"meta.export.tsx punctuation.definition.block\",\n			],\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"TypeScript[React] Punctuation Decorators\",\n			\"scope\": [\n				\"meta.decorator punctuation.decorator.ts\",\n				\"meta.decorator punctuation.decorator.tsx\",\n			],\n			\"settings\": {\n				\"foreground\": \"#82AAFF\",\n			},\n		},\n		{\n			\"name\": \"TypeScript[React] Punctuation Decorators\",\n			\"scope\": \"meta.tag.js meta.jsx.children.tsx\",\n			\"settings\": {\n				\"foreground\": \"#82AAFF\",\n			},\n		},\n		{\n			\"name\": \"YAML Entity Name Tags\",\n			\"scope\": \"entity.name.tag.yaml\",\n			\"settings\": {\n				\"foreground\": \"#7fdbca\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Variable Other ReadWrite\",\n			\"scope\": [\"variable.other.readwrite.js\", \"variable.parameter\"],\n			\"settings\": {\n				\"foreground\": \"#d7dbe0\",\n			},\n		},\n		{\n			\"name\": \"Support Class Component\",\n			\"scope\": [\"support.class.component.js\", \"support.class.component.tsx\"],\n			\"settings\": {\n				\"foreground\": \"#f78c6c\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Text nested in React tags\",\n			\"scope\": [\"meta.jsx.children\", \"meta.jsx.children.js\", \"meta.jsx.children.tsx\"],\n			\"settings\": {\n				\"foreground\": \"#d6deeb\",\n			},\n		},\n		{\n			\"name\": \"TypeScript Classes\",\n			\"scope\": \"meta.class entity.name.type.class.tsx\",\n			\"settings\": {\n				\"foreground\": \"#ffcb8b\",\n			},\n		},\n		{\n			\"name\": \"TypeScript Entity Name Type\",\n			\"scope\": [\"entity.name.type.tsx\", \"entity.name.type.module.tsx\"],\n			\"settings\": {\n				\"foreground\": \"#ffcb8b\",\n			},\n		},\n		{\n			\"name\": \"TypeScript Class Variable Keyword\",\n			\"scope\": [\n				\"meta.class.ts meta.var.expr.ts storage.type.ts\",\n				\"meta.class.tsx meta.var.expr.tsx storage.type.tsx\",\n			],\n			\"settings\": {\n				\"foreground\": \"#C792EA\",\n			},\n		},\n		{\n			\"name\": \"TypeScript Method Declaration e.g. `constructor`\",\n			\"scope\": [\n				\"meta.method.declaration storage.type.ts\",\n				\"meta.method.declaration storage.type.tsx\",\n			],\n			\"settings\": {\n				\"foreground\": \"#82AAFF\",\n			},\n		},\n		{\n			\"name\": \"normalize font style of certain components\",\n			\"scope\": [\n				\"meta.property-list.css meta.property-value.css variable.other.less\",\n				\"meta.property-list.scss variable.scss\",\n				\"meta.property-list.sass variable.sass\",\n				\"meta.brace\",\n				\"keyword.operator.operator\",\n				\"keyword.operator.or.regexp\",\n				\"keyword.operator.expression.in\",\n				\"keyword.operator.relational\",\n				\"keyword.operator.assignment\",\n				\"keyword.operator.comparison\",\n				\"keyword.operator.type\",\n				\"keyword.operator\",\n				\"keyword\",\n				\"punctuation.definition.string\",\n				\"punctuation\",\n				\"variable.other.readwrite.js\",\n				\"storage.type\",\n				\"source.css\",\n				\"string.quoted\",\n			],\n			\"settings\": {\n				\"fontStyle\": \"\",\n			},\n		},\n	],\n}\n";
//#endregion
//#region node_modules/@astrojs/starlight/integrations/expressive-code/themes/night-owl-light.jsonc?raw
var night_owl_light_default = "/**\n * Night Owl VS Code Theme - https://github.com/sdras/night-owl-vscode-theme\n *\n * MIT License\n *\n * Copyright (c) 2018 Sarah Drasner\n *\n * Permission is hereby granted, free of charge, to any person obtaining a copy\n * of this software and associated documentation files (the \"Software\"), to deal\n * in the Software without restriction, including without limitation the rights\n * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n * copies of the Software, and to permit persons to whom the Software is\n * furnished to do so, subject to the following conditions:\n *\n * The above copyright notice and this permission notice shall be included in all\n * copies or substantial portions of the Software.\n *\n * THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n * SOFTWARE.\n */\n{\n	\"name\": \"Night Owl Light\",\n	\"type\": \"light\",\n	\"semanticHighlighting\": false,\n	\"colors\": {\n		\"foreground\": \"#403f53\",\n		\"focusBorder\": \"#93A1A1\",\n		\"errorForeground\": \"#403f53\",\n		\"selection.background\": \"#7a8181ad\",\n		\"descriptionForeground\": \"#403f53\",\n		\"widget.shadow\": \"#d9d9d9\",\n		\"titleBar.activeBackground\": \"#F0F0F0\",\n		\"notifications.background\": \"#F0F0F0\",\n		\"notifications.foreground\": \"#403f53\",\n		\"notificationLink.foreground\": \"#994cc3\",\n		\"notifications.border\": \"#CCCCCC\",\n		\"notificationCenter.border\": \"#CCCCCC\",\n		\"notificationToast.border\": \"#CCCCCC\",\n		\"notificationCenterHeader.foreground\": \"#403f53\",\n		\"notificationCenterHeader.background\": \"#F0F0F0\",\n		\"button.background\": \"#2AA298\",\n		\"button.foreground\": \"#F0F0F0\",\n		\"dropdown.background\": \"#F0F0F0\",\n		\"dropdown.foreground\": \"#403f53\",\n		\"dropdown.border\": \"#d9d9d9\",\n		\"input.background\": \"#F0F0F0\",\n		\"input.foreground\": \"#403f53\",\n		\"input.border\": \"#d9d9d9\",\n		\"input.placeholderForeground\": \"#93A1A1\",\n		\"inputOption.activeBorder\": \"#2AA298\",\n		\"inputValidation.infoBorder\": \"#D0D0D0\",\n		\"inputValidation.infoBackground\": \"#F0F0F0\",\n		\"inputValidation.warningBackground\": \"#daaa01\",\n		\"inputValidation.warningBorder\": \"#E0AF02\",\n		\"inputValidation.errorBackground\": \"#f76e6e\",\n		\"inputValidation.errorBorder\": \"#de3d3b\",\n		\"badge.background\": \"#2AA298\",\n		\"badge.foreground\": \"#F0F0F0\",\n		\"progressBar.background\": \"#2AA298\",\n		\"list.activeSelectionBackground\": \"#d3e8f8\",\n		\"list.activeSelectionForeground\": \"#403f53\",\n		\"list.inactiveSelectionBackground\": \"#E0E7EA\",\n		\"list.inactiveSelectionForeground\": \"#403f53\",\n		\"list.focusBackground\": \"#d3e8f8\",\n		\"list.hoverBackground\": \"#d3e8f8\",\n		\"list.focusForeground\": \"#403f53\",\n		\"list.hoverForeground\": \"#403f53\",\n		\"list.highlightForeground\": \"#403f53\",\n		\"list.errorForeground\": \"#E64D49\",\n		\"list.warningForeground\": \"#daaa01\",\n		\"activityBar.background\": \"#F0F0F0\",\n		\"activityBar.foreground\": \"#403f53\",\n		\"activityBar.dropBackground\": \"#D0D0D0\",\n		\"activityBarBadge.background\": \"#403f53\",\n		\"activityBarBadge.foreground\": \"#F0F0F0\",\n		\"activityBar.border\": \"#F0F0F0\",\n		\"sideBar.background\": \"#F0F0F0\",\n		\"sideBar.foreground\": \"#403f53\",\n		\"sideBarTitle.foreground\": \"#403f53\",\n		\"sideBar.border\": \"#F0F0F0\",\n		\"scrollbar.shadow\": \"#CCCCCC\",\n		\"tab.border\": \"#F0F0F0\",\n		\"tab.activeBackground\": \"#F6F6F6\",\n		\"tab.activeForeground\": \"#403f53\",\n		\"tab.inactiveForeground\": \"#403f53\",\n		\"tab.inactiveBackground\": \"#F0F0F0\",\n		\"editorGroup.border\": \"#F0F0F0\",\n		\"editorGroup.background\": \"#F6F6F6\",\n		\"editorGroupHeader.tabsBackground\": \"#F0F0F0\",\n		\"editorGroupHeader.tabsBorder\": \"#F0F0F0\",\n		\"editorGroupHeader.noTabsBackground\": \"#F0F0F0\",\n		\"tab.activeModifiedBorder\": \"#2AA298\",\n		\"tab.inactiveModifiedBorder\": \"#93A1A1\",\n		\"tab.unfocusedActiveModifiedBorder\": \"#93A1A1\",\n		\"tab.unfocusedInactiveModifiedBorder\": \"#93A1A1\",\n		\"editor.background\": \"#FBFBFB\",\n		\"editor.foreground\": \"#403f53\",\n		\"editorCursor.foreground\": \"#90A7B2\",\n		\"editorLineNumber.foreground\": \"#90A7B2\",\n		\"editorLineNumber.activeForeground\": \"#403f53\",\n		\"editor.selectionBackground\": \"#E0E0E0\",\n		\"editor.selectionHighlightBackground\": \"#339cec33\",\n		\"editor.wordHighlightBackground\": \"#339cec33\",\n		\"editor.wordHighlightStrongBackground\": \"#007dd659\",\n		\"editor.findMatchBackground\": \"#93A1A16c\",\n		\"editor.findMatchHighlightBackground\": \"#93a1a16c\",\n		\"editor.findRangeHighlightBackground\": \"#7497a633\",\n		\"editor.hoverHighlightBackground\": \"#339cec33\",\n		\"editor.lineHighlightBackground\": \"#F0F0F0\",\n		\"editor.rangeHighlightBackground\": \"#7497a633\",\n		\"editorWhitespace.foreground\": \"#d9d9d9\",\n		\"editorIndentGuide.background\": \"#d9d9d9\",\n		\"editorCodeLens.foreground\": \"#403f53\",\n		\"editorBracketMatch.background\": \"#d3e8f8\",\n		\"editorBracketMatch.border\": \"#2AA298\",\n		\"editorError.foreground\": \"#E64D49\",\n		\"editorError.border\": \"#FBFBFB\",\n		\"editorWarning.foreground\": \"#daaa01\",\n		\"editorWarning.border\": \"#daaa01\",\n		\"editorGutter.addedBackground\": \"#49d0c5\",\n		\"editorGutter.modifiedBackground\": \"#6fbef6\",\n		\"editorGutter.deletedBackground\": \"#f76e6e\",\n		\"editorRuler.foreground\": \"#d9d9d9\",\n		\"editorOverviewRuler.errorForeground\": \"#E64D49\",\n		\"editorOverviewRuler.warningForeground\": \"#daaa01\",\n		\"editorWidget.background\": \"#F0F0F0\",\n		\"editorWidget.border\": \"#d9d9d9\",\n		\"editorSuggestWidget.background\": \"#F0F0F0\",\n		\"editorSuggestWidget.foreground\": \"#403f53\",\n		\"editorSuggestWidget.highlightForeground\": \"#403f53\",\n		\"editorSuggestWidget.selectedBackground\": \"#d3e8f8\",\n		\"editorSuggestWidget.border\": \"#d9d9d9\",\n		\"editorHoverWidget.background\": \"#F0F0F0\",\n		\"editorHoverWidget.border\": \"#d9d9d9\",\n		\"debugExceptionWidget.background\": \"#F0F0F0\",\n		\"debugExceptionWidget.border\": \"#d9d9d9\",\n		\"editorMarkerNavigation.background\": \"#D0D0D0\",\n		\"editorMarkerNavigationError.background\": \"#f76e6e\",\n		\"editorMarkerNavigationWarning.background\": \"#daaa01\",\n		\"debugToolBar.background\": \"#F0F0F0\",\n		\"pickerGroup.border\": \"#d9d9d9\",\n		\"pickerGroup.foreground\": \"#403f53\",\n		\"extensionButton.prominentBackground\": \"#2AA298\",\n		\"extensionButton.prominentForeground\": \"#F0F0F0\",\n		\"statusBar.background\": \"#F0F0F0\",\n		\"statusBar.border\": \"#F0F0F0\",\n		\"statusBar.debuggingBackground\": \"#F0F0F0\",\n		\"statusBar.debuggingForeground\": \"#403f53\",\n		\"statusBar.foreground\": \"#403f53\",\n		\"statusBar.noFolderBackground\": \"#F0F0F0\",\n		\"statusBar.noFolderForeground\": \"#403f53\",\n		\"panel.background\": \"#F0F0F0\",\n		\"panel.border\": \"#d9d9d9\",\n		\"peekView.border\": \"#d9d9d9\",\n		\"peekViewEditor.background\": \"#F6F6F6\",\n		\"peekViewEditorGutter.background\": \"#F6F6F6\",\n		\"peekViewEditor.matchHighlightBackground\": \"#49d0c5\",\n		\"peekViewResult.background\": \"#F0F0F0\",\n		\"peekViewResult.fileForeground\": \"#403f53\",\n		\"peekViewResult.lineForeground\": \"#403f53\",\n		\"peekViewResult.matchHighlightBackground\": \"#49d0c5\",\n		\"peekViewResult.selectionBackground\": \"#E0E7EA\",\n		\"peekViewResult.selectionForeground\": \"#403f53\",\n		\"peekViewTitle.background\": \"#F0F0F0\",\n		\"peekViewTitleLabel.foreground\": \"#403f53\",\n		\"peekViewTitleDescription.foreground\": \"#403f53\",\n		\"terminal.ansiBrightBlack\": \"#403f53\",\n		\"terminal.ansiBlack\": \"#403f53\",\n		\"terminal.ansiBrightBlue\": \"#288ed7\",\n		\"terminal.ansiBlue\": \"#288ed7\",\n		\"terminal.ansiBrightCyan\": \"#2AA298\",\n		\"terminal.ansiCyan\": \"#2AA298\",\n		\"terminal.ansiBrightGreen\": \"#08916a\",\n		\"terminal.ansiGreen\": \"#08916a\",\n		\"terminal.ansiBrightMagenta\": \"#d6438a\",\n		\"terminal.ansiMagenta\": \"#d6438a\",\n		\"terminal.ansiBrightRed\": \"#de3d3b\",\n		\"terminal.ansiRed\": \"#de3d3b\",\n		\"terminal.ansiBrightWhite\": \"#F0F0F0\",\n		\"terminal.ansiWhite\": \"#F0F0F0\",\n		\"terminal.ansiBrightYellow\": \"#daaa01\",\n		\"terminal.ansiYellow\": \"#E0AF02\",\n		\"terminal.background\": \"#F6F6F6\",\n		\"terminal.foreground\": \"#403f53\",\n	},\n	\"tokenColors\": [\n		{\n			\"name\": \"Changed\",\n			\"scope\": [\n				\"markup.changed\",\n				\"meta.diff.header.git\",\n				\"meta.diff.header.from-file\",\n				\"meta.diff.header.to-file\",\n			],\n			\"settings\": {\n				\"foreground\": \"#a2bffc\",\n			},\n		},\n		{\n			\"name\": \"Deleted\",\n			\"scope\": \"markup.deleted.diff\",\n			\"settings\": {\n				\"foreground\": \"#EF535090\",\n			},\n		},\n		{\n			\"name\": \"Inserted\",\n			\"scope\": \"markup.inserted.diff\",\n			\"settings\": {\n				\"foreground\": \"#4876d6ff\",\n			},\n		},\n		{\n			\"name\": \"Global settings\",\n			\"settings\": {\n				\"background\": \"#011627\",\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"Comment\",\n			\"scope\": \"comment\",\n			\"settings\": {\n				\"foreground\": \"#989fb1\",\n			},\n		},\n		{\n			\"name\": \"String\",\n			\"scope\": \"string\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"String Quoted\",\n			\"scope\": [\"string.quoted\", \"variable.other.readwrite.js\"],\n			\"settings\": {\n				\"foreground\": \"#c96765\",\n			},\n		},\n		{\n			\"name\": \"Support Constant Math\",\n			\"scope\": \"support.constant.math\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Number\",\n			\"scope\": [\"constant.numeric\", \"constant.character.numeric\"],\n			\"settings\": {\n				\"foreground\": \"#aa0982\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Built-in constant\",\n			\"scope\": [\"constant.language\", \"punctuation.definition.constant\", \"variable.other.constant\"],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"User-defined constant\",\n			\"scope\": [\"constant.character\", \"constant.other\"],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Constant Character Escape\",\n			\"scope\": \"constant.character.escape\",\n			\"settings\": {\n				\"foreground\": \"#aa0982\",\n			},\n		},\n		{\n			\"name\": \"RegExp String\",\n			\"scope\": [\"string.regexp\", \"string.regexp keyword.other\"],\n			\"settings\": {\n				\"foreground\": \"#5ca7e4\",\n			},\n		},\n		{\n			\"name\": \"Comma in functions\",\n			\"scope\": \"meta.function punctuation.separator.comma\",\n			\"settings\": {\n				\"foreground\": \"#5f7e97\",\n			},\n		},\n		{\n			\"name\": \"Variable\",\n			\"scope\": \"variable\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Keyword\",\n			\"scope\": [\"punctuation.accessor\", \"keyword\"],\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"Storage\",\n			\"scope\": [\n				\"storage\",\n				\"meta.var.expr\",\n				\"meta.class meta.method.declaration meta.var.expr storage.type.js\",\n				\"storage.type.property.js\",\n				\"storage.type.property.ts\",\n				\"storage.type.property.tsx\",\n			],\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"Storage type\",\n			\"scope\": \"storage.type\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"Storage type\",\n			\"scope\": \"storage.type.function.arrow.js\",\n			\"settings\": {\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Class name\",\n			\"scope\": [\"entity.name.class\", \"meta.class entity.name.type.class\"],\n			\"settings\": {\n				\"foreground\": \"#111111\",\n			},\n		},\n		{\n			\"name\": \"Inherited class\",\n			\"scope\": \"entity.other.inherited-class\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Function name\",\n			\"scope\": \"entity.name.function\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"Meta Tag\",\n			\"scope\": [\"punctuation.definition.tag\", \"meta.tag\"],\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"HTML Tag names\",\n			\"scope\": [\n				\"entity.name.tag\",\n				\"meta.tag.other.html\",\n				\"meta.tag.other.js\",\n				\"meta.tag.other.tsx\",\n				\"entity.name.tag.tsx\",\n				\"entity.name.tag.js\",\n				\"entity.name.tag\",\n				\"meta.tag.js\",\n				\"meta.tag.tsx\",\n				\"meta.tag.html\",\n			],\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"Tag attribute\",\n			\"scope\": \"entity.other.attribute-name\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Entity Name Tag Custom\",\n			\"scope\": \"entity.name.tag.custom\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Library (function & constant)\",\n			\"scope\": [\"support.function\", \"support.constant\"],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Support Constant Property Value meta\",\n			\"scope\": \"support.constant.meta.property-value\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"Library class/type\",\n			\"scope\": [\"support.type\", \"support.class\"],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Support Variable DOM\",\n			\"scope\": \"support.variable.dom\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Invalid\",\n			\"scope\": \"invalid\",\n			\"settings\": {\n				\"foreground\": \"#ff2c83\",\n			},\n		},\n		{\n			\"name\": \"Invalid deprecated\",\n			\"scope\": \"invalid.deprecated\",\n			\"settings\": {\n				\"foreground\": \"#d3423e\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator\",\n			\"scope\": \"keyword.operator\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator Relational\",\n			\"scope\": \"keyword.operator.relational\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator Assignment\",\n			\"scope\": \"keyword.operator.assignment\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator Arithmetic\",\n			\"scope\": \"keyword.operator.arithmetic\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator Bitwise\",\n			\"scope\": \"keyword.operator.bitwise\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator Increment\",\n			\"scope\": \"keyword.operator.increment\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator Ternary\",\n			\"scope\": \"keyword.operator.ternary\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"Double-Slashed Comment\",\n			\"scope\": \"comment.line.double-slash\",\n			\"settings\": {\n				\"foreground\": \"#939dbb\",\n			},\n		},\n		{\n			\"name\": \"Object\",\n			\"scope\": \"object\",\n			\"settings\": {\n				\"foreground\": \"#cdebf7\",\n			},\n		},\n		{\n			\"name\": \"Null\",\n			\"scope\": \"constant.language.null\",\n			\"settings\": {\n				\"foreground\": \"#bc5454\",\n			},\n		},\n		{\n			\"name\": \"Meta Brace\",\n			\"scope\": \"meta.brace\",\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"Meta Delimiter Period\",\n			\"scope\": \"meta.delimiter.period\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"Punctuation Definition String\",\n			\"scope\": \"punctuation.definition.string\",\n			\"settings\": {\n				\"foreground\": \"#111111\",\n			},\n		},\n		{\n			\"name\": \"Punctuation Definition String Markdown\",\n			\"scope\": \"punctuation.definition.string.begin.markdown\",\n			\"settings\": {\n				\"foreground\": \"#bc5454\",\n			},\n		},\n		{\n			\"name\": \"Boolean\",\n			\"scope\": \"constant.language.boolean\",\n			\"settings\": {\n				\"foreground\": \"#bc5454\",\n			},\n		},\n		{\n			\"name\": \"Object Comma\",\n			\"scope\": \"object.comma\",\n			\"settings\": {\n				\"foreground\": \"#ffffff\",\n			},\n		},\n		{\n			\"name\": \"Variable Parameter Function\",\n			\"scope\": \"variable.parameter.function\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Support Type Property Name & entity name tags\",\n			\"scope\": [\n				\"support.type.vendor.property-name\",\n				\"support.constant.vendor.property-value\",\n				\"support.type.property-name\",\n				\"meta.property-list entity.name.tag\",\n			],\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Entity Name tag reference in stylesheets\",\n			\"scope\": \"meta.property-list entity.name.tag.reference\",\n			\"settings\": {\n				\"foreground\": \"#57eaf1\",\n			},\n		},\n		{\n			\"name\": \"Constant Other Color RGB Value Punctuation Definition Constant\",\n			\"scope\": \"constant.other.color.rgb-value punctuation.definition.constant\",\n			\"settings\": {\n				\"foreground\": \"#aa0982\",\n			},\n		},\n		{\n			\"name\": \"Constant Other Color\",\n			\"scope\": \"constant.other.color\",\n			\"settings\": {\n				\"foreground\": \"#aa0982\",\n			},\n		},\n		{\n			\"name\": \"Keyword Other Unit\",\n			\"scope\": \"keyword.other.unit\",\n			\"settings\": {\n				\"foreground\": \"#aa0982\",\n			},\n		},\n		{\n			\"name\": \"Meta Selector\",\n			\"scope\": \"meta.selector\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"Entity Other Attribute Name Id\",\n			\"scope\": \"entity.other.attribute-name.id\",\n			\"settings\": {\n				\"foreground\": \"#aa0982\",\n			},\n		},\n		{\n			\"name\": \"Meta Property Name\",\n			\"scope\": \"meta.property-name\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"Doctypes\",\n			\"scope\": [\"entity.name.tag.doctype\", \"meta.tag.sgml.doctype\"],\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"Punctuation Definition Parameters\",\n			\"scope\": \"punctuation.definition.parameters\",\n			\"settings\": {\n				\"foreground\": \"#111111\",\n			},\n		},\n		{\n			\"name\": \"Keyword Control Operator\",\n			\"scope\": \"keyword.control.operator\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator Logical\",\n			\"scope\": \"keyword.operator.logical\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Variable Instances\",\n			\"scope\": [\n				\"variable.instance\",\n				\"variable.other.instance\",\n				\"variable.readwrite.instance\",\n				\"variable.other.readwrite.instance\",\n				\"variable.other.property\",\n			],\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"Variable Property Other object property\",\n			\"scope\": [\"variable.other.object.property\"],\n			\"settings\": {\n				\"foreground\": \"#111111\",\n			},\n		},\n		{\n			\"name\": \"Variable Property Other object\",\n			\"scope\": [\"variable.other.object.js\"],\n			\"settings\": {\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Entity Name Function\",\n			\"scope\": [\"entity.name.function\"],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Keyword Operator Comparison, imports, returns and Keyword Operator Ruby\",\n			\"scope\": [\n				\"keyword.operator.comparison\",\n				\"keyword.control.flow.js\",\n				\"keyword.control.flow.ts\",\n				\"keyword.control.flow.tsx\",\n				\"keyword.control.ruby\",\n				\"keyword.control.module.ruby\",\n				\"keyword.control.class.ruby\",\n				\"keyword.control.def.ruby\",\n				\"keyword.control.loop.js\",\n				\"keyword.control.loop.ts\",\n				\"keyword.control.import.js\",\n				\"keyword.control.import.ts\",\n				\"keyword.control.import.tsx\",\n				\"keyword.control.from.js\",\n				\"keyword.control.from.ts\",\n				\"keyword.control.from.tsx\",\n				\"keyword.operator.instanceof.js\",\n				\"keyword.operator.expression.instanceof.ts\",\n				\"keyword.operator.expression.instanceof.tsx\",\n			],\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"Keyword Control Conditional\",\n			\"scope\": [\n				\"keyword.control.conditional.js\",\n				\"keyword.control.conditional.ts\",\n				\"keyword.control.switch.js\",\n				\"keyword.control.switch.ts\",\n			],\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Support Constant, `new` keyword, Special Method Keyword, `debugger`, other keywords\",\n			\"scope\": [\n				\"support.constant\",\n				\"keyword.other.special-method\",\n				\"keyword.other.new\",\n				\"keyword.other.debugger\",\n				\"keyword.control\",\n			],\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"Support Function\",\n			\"scope\": \"support.function\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Invalid Broken\",\n			\"scope\": \"invalid.broken\",\n			\"settings\": {\n				\"foreground\": \"#aa0982\",\n			},\n		},\n		{\n			\"name\": \"Invalid Unimplemented\",\n			\"scope\": \"invalid.unimplemented\",\n			\"settings\": {\n				\"foreground\": \"#8BD649\",\n			},\n		},\n		{\n			\"name\": \"Invalid Illegal\",\n			\"scope\": \"invalid.illegal\",\n			\"settings\": {\n				\"foreground\": \"#c96765\",\n			},\n		},\n		{\n			\"name\": \"Language Variable\",\n			\"scope\": \"variable.language\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"Support Variable Property\",\n			\"scope\": \"support.variable.property\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"Variable Function\",\n			\"scope\": \"variable.function\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Variable Interpolation\",\n			\"scope\": \"variable.interpolation\",\n			\"settings\": {\n				\"foreground\": \"#ec5f67\",\n			},\n		},\n		{\n			\"name\": \"Meta Function Call\",\n			\"scope\": \"meta.function-call\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Punctuation Section Embedded\",\n			\"scope\": \"punctuation.section.embedded\",\n			\"settings\": {\n				\"foreground\": \"#d3423e\",\n			},\n		},\n		{\n			\"name\": \"Punctuation Tweaks\",\n			\"scope\": [\n				\"punctuation.terminator.expression\",\n				\"punctuation.definition.arguments\",\n				\"punctuation.definition.array\",\n				\"punctuation.section.array\",\n				\"meta.array\",\n			],\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"More Punctuation Tweaks\",\n			\"scope\": [\n				\"punctuation.definition.list.begin\",\n				\"punctuation.definition.list.end\",\n				\"punctuation.separator.arguments\",\n				\"punctuation.definition.list\",\n			],\n			\"settings\": {\n				\"foreground\": \"#111111\",\n			},\n		},\n		{\n			\"name\": \"Template Strings\",\n			\"scope\": \"string.template meta.template.expression\",\n			\"settings\": {\n				\"foreground\": \"#d3423e\",\n			},\n		},\n		{\n			\"name\": \"Backticks(``) in Template Strings\",\n			\"scope\": \"string.template punctuation.definition.string\",\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"Italics\",\n			\"scope\": \"italic\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n				\"fontStyle\": \"italic\",\n			},\n		},\n		{\n			\"name\": \"Bold\",\n			\"scope\": \"bold\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n				\"fontStyle\": \"bold\",\n			},\n		},\n		{\n			\"name\": \"Quote\",\n			\"scope\": \"quote\",\n			\"settings\": {\n				\"foreground\": \"#697098\",\n			},\n		},\n		{\n			\"name\": \"Raw Code\",\n			\"scope\": \"raw\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"CoffeeScript Variable Assignment\",\n			\"scope\": \"variable.assignment.coffee\",\n			\"settings\": {\n				\"foreground\": \"#31e1eb\",\n			},\n		},\n		{\n			\"name\": \"CoffeeScript Parameter Function\",\n			\"scope\": \"variable.parameter.function.coffee\",\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"CoffeeScript Assignments\",\n			\"scope\": \"variable.assignment.coffee\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"C# Readwrite Variables\",\n			\"scope\": \"variable.other.readwrite.cs\",\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"C# Classes & Storage types\",\n			\"scope\": [\"entity.name.type.class.cs\", \"storage.type.cs\"],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"C# Namespaces\",\n			\"scope\": \"entity.name.type.namespace.cs\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"Tag names in Stylesheets\",\n			\"scope\": [\n				\"entity.name.tag.css\",\n				\"entity.name.tag.less\",\n				\"entity.name.tag.custom.css\",\n				\"support.constant.property-value.css\",\n			],\n			\"settings\": {\n				\"foreground\": \"#c96765\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Wildcard(*) selector in Stylesheets\",\n			\"scope\": [\n				\"entity.name.tag.wildcard.css\",\n				\"entity.name.tag.wildcard.less\",\n				\"entity.name.tag.wildcard.scss\",\n				\"entity.name.tag.wildcard.sass\",\n			],\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"CSS Keyword Other Unit\",\n			\"scope\": \"keyword.other.unit.css\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Attribute Name for CSS\",\n			\"scope\": [\n				\"meta.attribute-selector.css entity.other.attribute-name.attribute\",\n				\"variable.other.readwrite.js\",\n			],\n			\"settings\": {\n				\"foreground\": \"#aa0982\",\n			},\n		},\n		{\n			\"name\": \"Elixir Classes\",\n			\"scope\": [\n				\"source.elixir support.type.elixir\",\n				\"source.elixir meta.module.elixir entity.name.class.elixir\",\n			],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Elixir Functions\",\n			\"scope\": \"source.elixir entity.name.function\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Elixir Constants\",\n			\"scope\": [\n				\"source.elixir constant.other.symbol.elixir\",\n				\"source.elixir constant.other.keywords.elixir\",\n			],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Elixir String Punctuations\",\n			\"scope\": \"source.elixir punctuation.definition.string\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Elixir\",\n			\"scope\": [\n				\"source.elixir variable.other.readwrite.module.elixir\",\n				\"source.elixir variable.other.readwrite.module.elixir punctuation.definition.variable.elixir\",\n			],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Elixir Binary Punctuations\",\n			\"scope\": \"source.elixir .punctuation.binary.elixir\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"Closure Constant Keyword\",\n			\"scope\": \"constant.keyword.clojure\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"Go Function Calls\",\n			\"scope\": \"source.go meta.function-call.go\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"Go Keywords\",\n			\"scope\": [\n				\"source.go keyword.package.go\",\n				\"source.go keyword.import.go\",\n				\"source.go keyword.function.go\",\n				\"source.go keyword.type.go\",\n				\"source.go keyword.struct.go\",\n				\"source.go keyword.interface.go\",\n				\"source.go keyword.const.go\",\n				\"source.go keyword.var.go\",\n				\"source.go keyword.map.go\",\n				\"source.go keyword.channel.go\",\n				\"source.go keyword.control.go\",\n			],\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"Go Constants e.g. nil, string format (%s, %d, etc.)\",\n			\"scope\": [\"source.go constant.language.go\", \"source.go constant.other.placeholder.go\"],\n			\"settings\": {\n				\"foreground\": \"#bc5454\",\n			},\n		},\n		{\n			\"name\": \"C++ Functions\",\n			\"scope\": [\"entity.name.function.preprocessor.cpp\", \"entity.scope.name.cpp\"],\n			\"settings\": {\n				\"foreground\": \"#0c969bff\",\n			},\n		},\n		{\n			\"name\": \"C++ Meta Namespace\",\n			\"scope\": [\"meta.namespace-block.cpp\"],\n			\"settings\": {\n				\"foreground\": \"#111111\",\n			},\n		},\n		{\n			\"name\": \"C++ Language Primitive Storage\",\n			\"scope\": [\"storage.type.language.primitive.cpp\"],\n			\"settings\": {\n				\"foreground\": \"#bc5454\",\n			},\n		},\n		{\n			\"name\": \"C++ Preprocessor Macro\",\n			\"scope\": [\"meta.preprocessor.macro.cpp\"],\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"C++ Variable Parameter\",\n			\"scope\": [\"variable.parameter\"],\n			\"settings\": {\n				\"foreground\": \"#111111\",\n			},\n		},\n		{\n			\"name\": \"Powershell Variables\",\n			\"scope\": [\"variable.other.readwrite.powershell\"],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Powershell Function\",\n			\"scope\": [\"support.function.powershell\"],\n			\"settings\": {\n				\"foreground\": \"#0c969bff\",\n			},\n		},\n		{\n			\"name\": \"ID Attribute Name in HTML\",\n			\"scope\": \"entity.other.attribute-name.id.html\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"HTML Punctuation Definition Tag\",\n			\"scope\": \"punctuation.definition.tag.html\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"HTML Doctype\",\n			\"scope\": \"meta.tag.sgml.doctype.html\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Classes\",\n			\"scope\": \"meta.class entity.name.type.class.js\",\n			\"settings\": {\n				\"foreground\": \"#111111\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Method Declaration e.g. `constructor`\",\n			\"scope\": \"meta.method.declaration storage.type.js\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Terminator\",\n			\"scope\": \"terminator.js\",\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Meta Punctuation Definition\",\n			\"scope\": \"meta.js punctuation.definition.js\",\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"Entity Names in Code Documentations\",\n			\"scope\": [\"entity.name.type.instance.jsdoc\", \"entity.name.type.instance.phpdoc\"],\n			\"settings\": {\n				\"foreground\": \"#5f7e97\",\n			},\n		},\n		{\n			\"name\": \"Other Variables in Code Documentations\",\n			\"scope\": [\"variable.other.jsdoc\", \"variable.other.phpdoc\"],\n			\"settings\": {\n				\"foreground\": \"#78ccf0\",\n			},\n		},\n		{\n			\"name\": \"JavaScript module imports and exports\",\n			\"scope\": [\n				\"variable.other.meta.import.js\",\n				\"meta.import.js variable.other\",\n				\"variable.other.meta.export.js\",\n				\"meta.export.js variable.other\",\n			],\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Variable Parameter Function\",\n			\"scope\": \"variable.parameter.function.js\",\n			\"settings\": {\n				\"foreground\": \"#7986E7\",\n			},\n		},\n		{\n			\"name\": \"JavaScript[React] Variable Other Object\",\n			\"scope\": [\n				\"variable.other.object.js\",\n				\"variable.other.object.jsx\",\n				\"variable.object.property.js\",\n				\"variable.object.property.jsx\",\n			],\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Variables\",\n			\"scope\": [\"variable.js\", \"variable.other.js\"],\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Entity Name Type\",\n			\"scope\": [\"entity.name.type.js\", \"entity.name.type.module.js\"],\n			\"settings\": {\n				\"foreground\": \"#111111\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Support Classes\",\n			\"scope\": \"support.class.js\",\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"JSON Property Names\",\n			\"scope\": \"support.type.property-name.json\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"JSON Support Constants\",\n			\"scope\": \"support.constant.json\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"JSON Property values (string)\",\n			\"scope\": \"meta.structure.dictionary.value.json string.quoted.double\",\n			\"settings\": {\n				\"foreground\": \"#c789d6\",\n			},\n		},\n		{\n			\"name\": \"Strings in JSON values\",\n			\"scope\": \"string.quoted.double.json punctuation.definition.string.json\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"Specific JSON Property values like null\",\n			\"scope\": \"meta.structure.dictionary.json meta.structure.dictionary.value constant.language\",\n			\"settings\": {\n				\"foreground\": \"#bc5454\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Other Variable\",\n			\"scope\": \"variable.other.object.js\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"Ruby Variables\",\n			\"scope\": [\"variable.other.ruby\"],\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"Ruby Class\",\n			\"scope\": [\"entity.name.type.class.ruby\"],\n			\"settings\": {\n				\"foreground\": \"#c96765\",\n			},\n		},\n		{\n			\"name\": \"Ruby Hashkeys\",\n			\"scope\": \"constant.language.symbol.hashkey.ruby\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"Ruby Symbols\",\n			\"scope\": \"constant.language.symbol.ruby\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"LESS Tag names\",\n			\"scope\": \"entity.name.tag.less\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"LESS Keyword Other Unit\",\n			\"scope\": \"keyword.other.unit.css\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"Attribute Name for LESS\",\n			\"scope\": \"meta.attribute-selector.less entity.other.attribute-name.attribute\",\n			\"settings\": {\n				\"foreground\": \"#aa0982\",\n			},\n		},\n		{\n			\"name\": \"Markdown Headings\",\n			\"scope\": [\n				\"markup.heading.markdown\",\n				\"markup.heading.setext.1.markdown\",\n				\"markup.heading.setext.2.markdown\",\n			],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Markdown Italics\",\n			\"scope\": \"markup.italic.markdown\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n				\"fontStyle\": \"italic\",\n			},\n		},\n		{\n			\"name\": \"Markdown Bold\",\n			\"scope\": \"markup.bold.markdown\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n				\"fontStyle\": \"bold\",\n			},\n		},\n		{\n			\"name\": \"Markdown Quote + others\",\n			\"scope\": \"markup.quote.markdown\",\n			\"settings\": {\n				\"foreground\": \"#697098\",\n			},\n		},\n		{\n			\"name\": \"Markdown Raw Code + others\",\n			\"scope\": \"markup.inline.raw.markdown\",\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"Markdown Links\",\n			\"scope\": [\"markup.underline.link.markdown\", \"markup.underline.link.image.markdown\"],\n			\"settings\": {\n				\"foreground\": \"#ff869a\",\n			},\n		},\n		{\n			\"name\": \"Markdown Link Title and Description\",\n			\"scope\": [\"string.other.link.title.markdown\", \"string.other.link.description.markdown\"],\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"Markdown Punctuation\",\n			\"scope\": [\n				\"punctuation.definition.string.markdown\",\n				\"punctuation.definition.string.begin.markdown\",\n				\"punctuation.definition.string.end.markdown\",\n				\"meta.link.inline.markdown punctuation.definition.string\",\n			],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Markdown MetaData Punctuation\",\n			\"scope\": [\"punctuation.definition.metadata.markdown\"],\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"Markdown List Punctuation\",\n			\"scope\": [\"beginning.punctuation.definition.list.markdown\"],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Markdown Inline Raw String\",\n			\"scope\": \"markup.inline.raw.string.markdown\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"PHP Variables\",\n			\"scope\": [\"variable.other.php\", \"variable.other.property.php\"],\n			\"settings\": {\n				\"foreground\": \"#111111\",\n			},\n		},\n		{\n			\"name\": \"Support Classes in PHP\",\n			\"scope\": \"support.class.php\",\n			\"settings\": {\n				\"foreground\": \"#111111\",\n			},\n		},\n		{\n			\"name\": \"Punctuations in PHP function calls\",\n			\"scope\": \"meta.function-call.php punctuation\",\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"PHP Global Variables\",\n			\"scope\": \"variable.other.global.php\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Declaration Punctuation in PHP Global Variables\",\n			\"scope\": \"variable.other.global.php punctuation.definition.variable\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Language Constants in Python\",\n			\"scope\": \"constant.language.python\",\n			\"settings\": {\n				\"foreground\": \"#bc5454\",\n			},\n		},\n		{\n			\"name\": \"Python Function Parameter and Arguments\",\n			\"scope\": [\"variable.parameter.function.python\", \"meta.function-call.arguments.python\"],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Python Function Call\",\n			\"scope\": [\"meta.function-call.python\", \"meta.function-call.generic.python\"],\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"Punctuations in Python\",\n			\"scope\": \"punctuation.python\",\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"Decorator Functions in Python\",\n			\"scope\": \"entity.name.function.decorator.python\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Python Language Variable\",\n			\"scope\": \"source.python variable.language.special\",\n			\"settings\": {\n				\"foreground\": \"#aa0982\",\n			},\n		},\n		{\n			\"name\": \"Python import control keyword\",\n			\"scope\": \"keyword.control\",\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"SCSS Variable\",\n			\"scope\": [\n				\"variable.scss\",\n				\"variable.sass\",\n				\"variable.parameter.url.scss\",\n				\"variable.parameter.url.sass\",\n			],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Variables in SASS At-Rules\",\n			\"scope\": [\"source.css.scss meta.at-rule variable\", \"source.css.sass meta.at-rule variable\"],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"Variables in SASS At-Rules\",\n			\"scope\": [\"source.css.scss meta.at-rule variable\", \"source.css.sass meta.at-rule variable\"],\n			\"settings\": {\n				\"foreground\": \"#111111\",\n			},\n		},\n		{\n			\"name\": \"Attribute Name for SASS\",\n			\"scope\": [\n				\"meta.attribute-selector.scss entity.other.attribute-name.attribute\",\n				\"meta.attribute-selector.sass entity.other.attribute-name.attribute\",\n			],\n			\"settings\": {\n				\"foreground\": \"#aa0982\",\n			},\n		},\n		{\n			\"name\": \"Tag names in SASS\",\n			\"scope\": [\"entity.name.tag.scss\", \"entity.name.tag.sass\"],\n			\"settings\": {\n				\"foreground\": \"#0c969b\",\n			},\n		},\n		{\n			\"name\": \"SASS Keyword Other Unit\",\n			\"scope\": [\"keyword.other.unit.scss\", \"keyword.other.unit.sass\"],\n			\"settings\": {\n				\"foreground\": \"#994cc3\",\n			},\n		},\n		{\n			\"name\": \"TypeScript[React] Variables and Object Properties\",\n			\"scope\": [\n				\"variable.other.readwrite.alias.ts\",\n				\"variable.other.readwrite.alias.tsx\",\n				\"variable.other.readwrite.ts\",\n				\"variable.other.readwrite.tsx\",\n				\"variable.other.object.ts\",\n				\"variable.other.object.tsx\",\n				\"variable.object.property.ts\",\n				\"variable.object.property.tsx\",\n				\"variable.other.ts\",\n				\"variable.other.tsx\",\n				\"variable.tsx\",\n				\"variable.ts\",\n			],\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"TypeScript[React] Entity Name Types\",\n			\"scope\": [\"entity.name.type.ts\", \"entity.name.type.tsx\"],\n			\"settings\": {\n				\"foreground\": \"#111111\",\n			},\n		},\n		{\n			\"name\": \"TypeScript[React] Node Classes\",\n			\"scope\": [\"support.class.node.ts\", \"support.class.node.tsx\"],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"TypeScript[React] Entity Name Types as Parameters\",\n			\"scope\": [\n				\"meta.type.parameters.ts entity.name.type\",\n				\"meta.type.parameters.tsx entity.name.type\",\n			],\n			\"settings\": {\n				\"foreground\": \"#5f7e97\",\n			},\n		},\n		{\n			\"name\": \"TypeScript[React] Import/Export Punctuations\",\n			\"scope\": [\n				\"meta.import.ts punctuation.definition.block\",\n				\"meta.import.tsx punctuation.definition.block\",\n				\"meta.export.ts punctuation.definition.block\",\n				\"meta.export.tsx punctuation.definition.block\",\n			],\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"TypeScript[React] Punctuation Decorators\",\n			\"scope\": [\n				\"meta.decorator punctuation.decorator.ts\",\n				\"meta.decorator punctuation.decorator.tsx\",\n			],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"TypeScript[React] Punctuation Decorators\",\n			\"scope\": \"meta.tag.js meta.jsx.children.tsx\",\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"YAML Entity Name Tags\",\n			\"scope\": \"entity.name.tag.yaml\",\n			\"settings\": {\n				\"foreground\": \"#111111\",\n			},\n		},\n		{\n			\"name\": \"JavaScript Variable Other ReadWrite\",\n			\"scope\": [\"variable.other.readwrite.js\", \"variable.parameter\"],\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"Support Class Component\",\n			\"scope\": [\"support.class.component.js\", \"support.class.component.tsx\"],\n			\"settings\": {\n				\"foreground\": \"#aa0982\",\n				\"fontStyle\": \"\",\n			},\n		},\n		{\n			\"name\": \"Text nested in React tags\",\n			\"scope\": [\"meta.jsx.children\", \"meta.jsx.children.js\", \"meta.jsx.children.tsx\"],\n			\"settings\": {\n				\"foreground\": \"#403f53\",\n			},\n		},\n		{\n			\"name\": \"TypeScript Classes\",\n			\"scope\": \"meta.class entity.name.type.class.tsx\",\n			\"settings\": {\n				\"foreground\": \"#111111\",\n			},\n		},\n		{\n			\"name\": \"TypeScript Entity Name Type\",\n			\"scope\": [\"entity.name.type.tsx\", \"entity.name.type.module.tsx\"],\n			\"settings\": {\n				\"foreground\": \"#111111\",\n			},\n		},\n		{\n			\"name\": \"TypeScript Class Variable Keyword\",\n			\"scope\": [\n				\"meta.class.ts meta.var.expr.ts storage.type.ts\",\n				\"meta.class.tsx meta.var.expr.tsx storage.type.tsx\",\n			],\n			\"settings\": {\n				\"foreground\": \"#C792EA\",\n			},\n		},\n		{\n			\"name\": \"TypeScript Method Declaration e.g. `constructor`\",\n			\"scope\": [\n				\"meta.method.declaration storage.type.ts\",\n				\"meta.method.declaration storage.type.tsx\",\n			],\n			\"settings\": {\n				\"foreground\": \"#4876d6\",\n			},\n		},\n		{\n			\"name\": \"normalize font style of certain components\",\n			\"scope\": [\n				\"meta.property-list.css meta.property-value.css variable.other.less\",\n				\"meta.property-list.scss variable.scss\",\n				\"meta.property-list.sass variable.sass\",\n				\"meta.brace\",\n				\"keyword.operator.operator\",\n				\"keyword.operator.or.regexp\",\n				\"keyword.operator.expression.in\",\n				\"keyword.operator.relational\",\n				\"keyword.operator.assignment\",\n				\"keyword.operator.comparison\",\n				\"keyword.operator.type\",\n				\"keyword.operator\",\n				\"keyword\",\n				\"punctuation.definition.string\",\n				\"punctuation\",\n				\"variable.other.readwrite.js\",\n				\"storage.type\",\n				\"source.css\",\n				\"string.quoted\",\n			],\n			\"settings\": {\n				\"fontStyle\": \"\",\n			},\n		},\n	],\n}\n";
//#endregion
//#region node_modules/@astrojs/starlight/integrations/expressive-code/theming.ts
/**
* Converts the Starlight `themes` config option into a format understood by Expressive Code,
* loading any bundled themes and using the Starlight defaults if no themes were provided.
*/
function preprocessThemes(themes) {
	themes = themes && !Array.isArray(themes) ? [themes] : themes;
	if (!themes || !themes.length) themes = ["starlight-dark", "starlight-light"];
	return themes.map((theme) => {
		if (theme === "starlight-dark" || theme === "starlight-light") {
			const bundledTheme = theme === "starlight-dark" ? night_owl_dark_default : night_owl_light_default;
			return customizeBundledTheme(dist_exports.ExpressiveCodeTheme.fromJSONString(bundledTheme));
		}
		return theme;
	});
}
/**
* Customizes some settings of the bundled theme to make it fit better with Starlight.
*/
function customizeBundledTheme(theme) {
	theme.colors["titleBar.border"] = theme.colors["tab.activeBackground"];
	theme.colors["editorGroupHeader.tabsBorder"] = theme.colors["tab.activeBackground"];
	theme.settings.forEach((s) => {
		if (s.name?.includes("Link")) s.settings.fontStyle = "underline";
	});
	return theme;
}
/**
* Modifies the given theme by applying Starlight's CSS variables to the colors of UI elements
* (backgrounds, buttons, shadows etc.). This ensures that code blocks match the site's theme.
*/
function applyStarlightUiThemeColors(theme) {
	const isDark = theme.type === "dark";
	const neutralMinimal = isDark ? "#ffffff17" : "#0000001a";
	const neutralDimmed = isDark ? "#ffffff40" : "#00000055";
	const borderColor = "color-mix(in srgb, var(--sl-color-gray-5), transparent 25%)";
	theme.colors["titleBar.border"] = borderColor;
	theme.colors["editorGroupHeader.tabsBorder"] = borderColor;
	const backgroundColor = isDark ? "var(--sl-color-black)" : "var(--sl-color-gray-6)";
	theme.colors["titleBar.activeBackground"] = backgroundColor;
	theme.colors["editorGroupHeader.tabsBackground"] = backgroundColor;
	theme.colors["titleBar.activeForeground"] = "var(--sl-color-text)";
	theme.colors["tab.activeForeground"] = "var(--sl-color-text)";
	const activeBorderColor = isDark ? "var(--sl-color-accent-high)" : "var(--sl-color-accent)";
	theme.colors["tab.activeBorder"] = "transparent";
	theme.colors["tab.activeBorderTop"] = activeBorderColor;
	theme.colors["scrollbarSlider.background"] = neutralMinimal;
	theme.colors["scrollbarSlider.hoverBackground"] = neutralDimmed;
	theme.bg = isDark ? "#23262f" : "#f6f7f9";
	theme.colors["editor.background"] = theme.bg;
	const editorBackgroundColor = isDark ? "var(--sl-color-gray-6)" : "var(--sl-color-gray-7)";
	theme.styleOverrides.frames = {
		editorBackground: editorBackgroundColor,
		terminalBackground: editorBackgroundColor,
		editorActiveTabBackground: editorBackgroundColor,
		terminalTitlebarDotsForeground: borderColor,
		terminalTitlebarDotsOpacity: "0.75",
		inlineButtonForeground: "var(--sl-color-text)",
		frameBoxShadowCssValue: "none"
	};
	theme.styleOverrides.textMarkers = {
		markBackground: neutralMinimal,
		markBorderColor: neutralDimmed
	};
	return theme;
}
//#endregion
//#region node_modules/@astrojs/starlight/integrations/shared/localeToLang.ts
/**
* Get the BCP-47 language tag for the given locale.
* @param locale Locale string or `undefined` for the root locale.
*/
function localeToLang(config, locale) {
	const lang = locale ? config.locales?.[locale]?.lang : config.locales?.root?.lang;
	const defaultLang = config.defaultLocale?.lang || config.defaultLocale?.locale;
	return lang || defaultLang || BuiltInDefaultLocale.lang;
}
//#endregion
//#region node_modules/@astrojs/starlight/integrations/expressive-code/translations.ts
function addTranslations(config, useTranslations) {
	addTranslationsForLocale(config.defaultLocale.locale, config, useTranslations);
	if (config.isMultilingual) for (const locale in config.locales) {
		if (locale === config.defaultLocale.locale || locale === "root") continue;
		addTranslationsForLocale(locale, config, useTranslations);
	}
}
function addTranslationsForLocale(locale, config, useTranslations) {
	const lang = localeToLang(config, locale);
	const t = useTranslations(lang);
	[
		"expressiveCode.copyButtonCopied",
		"expressiveCode.copyButtonTooltip",
		"expressiveCode.terminalWindowFallbackTitle"
	].forEach((key) => {
		const translation = t.exists(key) ? t(key) : void 0;
		if (!translation) return;
		const ecId = key.replace(/^expressiveCode\./, "");
		dist_exports.pluginFramesTexts.overrideTexts(lang, { [ecId]: translation });
	});
}
//#endregion
//#region node_modules/@astrojs/starlight/integrations/shared/slugToLocale.ts
/**
* Get the “locale” of a slug. This is the base path at which a language is served.
* For example, if French docs are in `src/content/docs/french/`, the locale is `french`.
* Root locale slugs will return `undefined`.
* @param slug A collection entry slug
*/
function slugToLocale(slug, config) {
	const localesConfig = config.locales ?? {};
	const baseSegment = slug?.split("/")[0];
	if (baseSegment && localesConfig[baseSegment]) return baseSegment;
	if (!localesConfig.root) return config.defaultLocale.locale;
}
//#endregion
//#region node_modules/@astrojs/starlight/integrations/shared/absolutePathToLang.ts
/** Get current language from an absolute file path. */
function absolutePathToLang(path, { docsPath, starlightConfig }) {
	path = path?.replace(/\\/g, "/");
	if (path && !path.startsWith("/") && docsPath.startsWith("/")) path = "/" + path;
	const slug = path?.replace(docsPath, "");
	return localeToLang(starlightConfig, slugToLocale(slug, starlightConfig));
}
//#endregion
//#region node_modules/@astrojs/starlight/integrations/expressive-code/preprocessor.ts
/**
* Create an Expressive Code configuration preprocessor based on Starlight config.
* Used internally to set up Expressive Code and by the `<Code>` component.
*/
function getStarlightEcConfigPreprocessor({ docsPath, starlightConfig, useTranslations }) {
	return (input) => {
		const ecConfig = input.ecConfig;
		const { themes: themesInput, cascadeLayer, customizeTheme, styleOverrides: { textMarkers: textMarkersStyleOverrides, ...otherStyleOverrides } = {}, useStarlightDarkModeSwitch, useStarlightUiThemeColors = ecConfig.themes === void 0, plugins = [], ...rest } = ecConfig;
		const themes = preprocessThemes(themesInput);
		if (useStarlightUiThemeColors === true && themes.length < 2) console.warn("*** Warning: Using the config option \"useStarlightUiThemeColors: true\" with a single theme is not recommended. For better color contrast, please provide at least one dark and one light theme.\n");
		plugins.push({
			name: "Starlight Plugin",
			hooks: { postprocessRenderedBlock: ({ renderData }) => {
				(0, hast_exports.addClassName)(renderData.blockAst, "not-content");
			} }
		});
		addTranslations(starlightConfig, useTranslations);
		return {
			themes,
			customizeTheme: (theme) => {
				if (useStarlightUiThemeColors) applyStarlightUiThemeColors(theme);
				if (customizeTheme) theme = customizeTheme(theme) ?? theme;
				return theme;
			},
			defaultLocale: starlightConfig.defaultLocale?.lang ?? starlightConfig.defaultLocale?.locale,
			themeCssSelector: (theme, { styleVariants }) => {
				if (useStarlightDarkModeSwitch !== false && styleVariants.length >= 2) {
					const baseTheme = styleVariants[0]?.theme;
					const altTheme = styleVariants.find((v) => v.theme.type !== baseTheme?.type)?.theme;
					if (theme === baseTheme || theme === altTheme) return `[data-theme='${theme.type}']`;
				}
				return `[data-theme='${theme.name}']`;
			},
			cascadeLayer: cascadeLayer ?? "starlight.components",
			styleOverrides: {
				borderRadius: "0px",
				borderWidth: "1px",
				codePaddingBlock: "0.75rem",
				codePaddingInline: "1rem",
				codeFontFamily: "var(--__sl-font-mono)",
				codeFontSize: "var(--sl-text-code)",
				codeLineHeight: "var(--sl-line-height)",
				uiFontFamily: "var(--__sl-font)",
				textMarkers: {
					lineDiffIndicatorMarginLeft: "0.25rem",
					defaultChroma: "45",
					backgroundOpacity: "60%",
					...textMarkersStyleOverrides
				},
				...otherStyleOverrides
			},
			getBlockLocale: ({ file }) => {
				if (file.url) return localeToLang(starlightConfig, slugToLocale(file.url.pathname.slice(1), starlightConfig));
				return absolutePathToLang(file.path, {
					docsPath,
					starlightConfig
				});
			},
			plugins,
			...rest
		};
	};
}
//#endregion
//#region \0virtual:astro-expressive-code/preprocess-config
var preprocess_config_default = getStarlightEcConfigPreprocessor({
	docsPath: "/C:/Users/keith/Desktop/KéireEngine/Services/KeireDistributionService/DocumentationSite/Source/content/docs/",
	starlightConfig: user_config_default,
	useTranslations
});
//#endregion
export { preprocess_config_default as default };
