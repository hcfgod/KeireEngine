import { f as removeBase, l as isRemotePath } from "./path_DV0dTggT.mjs";
import { O as unescapeHTML, _ as generateCspDigest, m as renderTemplate, r as spreadAttributes } from "./server_DvX7bpsP.mjs";
import { Q as UnknownContentCollectionError, n as AstroUserError, t as AstroError } from "./errors_BdwoJ0rW.mjs";
import { t as createComponent } from "./astro-component_CT_H5Ga1.mjs";
import { r as VALID_INPUT_FORMATS } from "./consts_Bx4_lkUX.mjs";
import * as devalue from "devalue";
import "html-escaper";
import * as z from "zod/v4";
import i18next from "i18next";
//#region node_modules/neotraverse/dist/path-Bxt08XGL.js
var to_string = (obj) => Object.prototype.toString.call(obj);
var is_typed_array = (value) => ArrayBuffer.isView(value) && to_string(value) !== "[object DataView]";
var is_array = Array.isArray;
var is_boxed_primitive = (obj) => {
	const tag = to_string(obj);
	if (tag !== "[object Boolean]" && tag !== "[object Number]" && tag !== "[object String]") return false;
	try {
		return typeof obj.valueOf() !== "object";
	} catch {
		return false;
	}
};
var gopd = Object.getOwnPropertyDescriptor;
var is_property_enumerable = Object.prototype.propertyIsEnumerable;
var get_own_property_symbols = Object.getOwnPropertySymbols;
var has_own_property = Object.prototype.hasOwnProperty;
var object_keys = Object.keys;
var object_proto = Object.prototype;
var get_proto = Object.getPrototypeOf;
function safe_set(dst, key, value) {
	if (typeof key === "object" && key !== null) key = String(key);
	if (key === "__proto__") Object.defineProperty(dst, key, {
		value,
		writable: true,
		enumerable: true,
		configurable: true
	});
	else dst[key] = value;
}
function assert_within_depth(depth, max_depth) {
	if (max_depth !== void 0 && depth > max_depth) throw new RangeError(`neotraverse: maximum traversal depth (${max_depth}) exceeded`);
}
function own_enumerable_keys(obj) {
	const res = object_keys(obj);
	const symbols = get_own_property_symbols(obj);
	for (let i = 0; i < symbols.length; i++) if (is_property_enumerable.call(obj, symbols[i])) res.push(symbols[i]);
	return res;
}
function is_non_writable(object, key) {
	return !gopd(object, key)?.writable;
}
var empty_null = {
	includeSymbols: false,
	immutable: false
};
function clamp_concurrency(c) {
	return typeof c === "number" && c >= 1 ? Math.floor(c) : 1;
}
function array_numeric(keys) {
	for (let i = 0; i < keys.length; i++) {
		const k = keys[i];
		if (typeof k === "string" && "" + +k === k) keys[i] = +k;
	}
	return keys;
}
function array_keys(node, keys) {
	const len = node.length;
	if (keys.length === len) {
		for (let i = 0; i < len; i++) keys[i] = i;
		return keys;
	}
	return array_numeric(keys);
}
function map_set_child_keys(node) {
	if (node instanceof Map) {
		const keys = [];
		for (const k of node.keys()) keys.push(k);
		return keys;
	}
	const keys = [];
	let i = 0;
	for (const _ of node) {
		keys.push(i);
		i++;
	}
	return keys;
}
function get_child_at(node, key, descend_map_set) {
	if (descend_map_set && node instanceof Map) return node.get(key);
	if (descend_map_set && node instanceof Set) return [...node][key];
	return node[key];
}
var shell_keyed = false;
function make_shell(src) {
	if (is_array(src)) {
		shell_keyed = true;
		return new Array(src.length);
	}
	shell_keyed = false;
	if (src instanceof ArrayBuffer) return src.slice(0);
	if (src instanceof DataView) return new DataView(src.buffer.slice(src.byteOffset, src.byteOffset + src.byteLength), 0, src.byteLength);
	if (is_typed_array(src)) return src.slice();
	if (is_boxed_primitive(src)) return Object(src);
	if (src instanceof Map) return new Map(src);
	if (src instanceof Set) return new Set(src);
	if (src instanceof WeakMap || src instanceof WeakSet) return src;
	const tag = to_string(src);
	if (tag === "[object Date]" && typeof src.getTime === "function") {
		shell_keyed = true;
		return new Date(src.getTime());
	}
	if (tag === "[object RegExp]" && typeof src.source === "string") try {
		const re = new RegExp(src.source, src.flags);
		re.lastIndex = src.lastIndex;
		shell_keyed = true;
		return re;
	} catch {}
	if (tag === "[object Error]") {
		const Ctor = typeof src.constructor === "function" ? src.constructor : Error;
		let dst;
		try {
			dst = new Ctor(src.message);
		} catch {
			dst = new Error(src.message);
		}
		if (src.name !== dst.name) dst.name = src.name;
		if (src.stack !== void 0) dst.stack = src.stack;
		if ("cause" in src) dst.cause = src.cause;
		shell_keyed = true;
		return dst;
	}
	if (tag === "[object Map]") {
		shell_keyed = false;
		return new Map(src);
	}
	if (tag === "[object Set]") {
		shell_keyed = false;
		return new Set(src);
	}
	if (tag === "[object ArrayBuffer]") {
		shell_keyed = false;
		return src.slice(0);
	}
	if (tag === "[object DataView]") {
		shell_keyed = false;
		const dv = src;
		return new DataView(dv.buffer.slice(dv.byteOffset, dv.byteOffset + dv.byteLength), 0, dv.byteLength);
	}
	shell_keyed = true;
	const proto = get_proto(src);
	return proto === object_proto ? {} : Object.create(proto);
}
function copy(src, options) {
	if (typeof src === "object" && src !== null) {
		const dst = make_shell(src);
		if (!shell_keyed) return dst;
		const keys = options.includeSymbols ? own_enumerable_keys(src) : object_keys(src);
		for (let i = 0; i < keys.length; i++) {
			const key = keys[i];
			safe_set(dst, key, src[key]);
		}
		return dst;
	}
	return src;
}
/**
* The traversal context. Every method lives on the prototype, so visiting a
* node allocates a *single* object — not a context object plus a fresh closure
* for each of `update`/`remove`/`before`/… and a separate `modifiers` object.
* That, plus the lazily-derived {@link path}, is what makes the modern build
* dramatically faster and lighter on the GC than the classic design.
*
* @see https://neotraverse.puruvj.dev/guide/context
*/
var WalkContext = class {
	node;
	node_;
	parent;
	key;
	isRoot;
	isLeaf = false;
	isFirst = false;
	isLast = false;
	level;
	circular = void 0;
	keys = null;
	w;
	keep_going = true;
	removed = false;
	mods = null;
	constructor(w, node_, node) {
		const path = w.path;
		const level = path.length;
		this.w = w;
		this.node = node;
		this.node_ = node_;
		this.parent = w.parents[level - 1];
		this.key = path[level - 1];
		this.isRoot = level === 0;
		this.level = level;
	}
	get parents() {
		return this.w.parents;
	}
	get notRoot() {
		return !this.isRoot;
	}
	get notLeaf() {
		return !this.isLeaf;
	}
	get path() {
		const out = new Array(this.level);
		let c = this;
		for (let i = this.level - 1; i >= 0; i--) {
			out[i] = c.key;
			c = c.parent;
		}
		return out;
	}
	update(x, stopHere = false) {
		if (!this.isRoot) safe_set(this.parent.node, this.key, x);
		this.node = x;
		if (stopHere) this.keep_going = false;
	}
	delete(stopHere) {
		delete this.parent.node[this.key];
		this.removed = true;
		if (stopHere) this.keep_going = false;
	}
	remove(stopHere) {
		const parent = this.parent.node;
		if (is_array(parent)) parent.splice(this.key, 1);
		else delete parent[this.key];
		this.removed = true;
		if (stopHere) this.keep_going = false;
	}
	before(f) {
		(this.mods ??= {}).before = f;
	}
	after(f) {
		(this.mods ??= {}).after = f;
	}
	pre(f) {
		(this.mods ??= {}).pre = f;
	}
	post(f) {
		(this.mods ??= {}).post = f;
	}
	stop() {
		this.w.alive = false;
	}
	block() {
		this.keep_going = false;
	}
	nextSibling() {
		const parent = this.parent;
		if (!parent?.keys || this.key === void 0) return void 0;
		const keys = parent.keys;
		const idx = keys.indexOf(this.key);
		if (idx < 0 || idx >= keys.length - 1) return void 0;
		return make_sibling_ctx(this, keys[idx + 1]);
	}
	prevSibling() {
		const parent = this.parent;
		if (!parent?.keys || this.key === void 0) return void 0;
		const keys = parent.keys;
		const idx = keys.indexOf(this.key);
		if (idx <= 0) return void 0;
		return make_sibling_ctx(this, keys[idx - 1]);
	}
};
function make_sibling_ctx(self, sibKey) {
	const w = self.w;
	const parent = self.parent;
	const sibNode = get_child_at(parent.node, sibKey, w.descend_map_set);
	const sib = new WalkContext(w, sibNode, sibNode);
	sib.parent = parent;
	sib.key = sibKey;
	sib.level = self.level;
	sib.isRoot = self.level === 0;
	if (typeof sibNode === "object" && sibNode !== null) {
		sib.keys = initial_keys(w, sibNode, w.iter);
		sib.isLeaf = sib.keys.length === 0;
	} else sib.isLeaf = true;
	return sib;
}
function make_walk_state(options = empty_null, immutable) {
	return {
		alive: true,
		immutable: immutable ?? !!options.immutable,
		iter: options.includeSymbols ? own_enumerable_keys : object_keys,
		max_depth: options.maxDepth,
		path: [],
		parents: [],
		descend_map_set: !!options.descendIntoMapSet,
		concurrency: clamp_concurrency(options.concurrency)
	};
}
function descend_children(w, ctx, node, walker, immutable, mods, fresh) {
	const { path } = w;
	const pre = mods !== null ? mods.pre : void 0;
	const post = mods !== null ? mods.post : void 0;
	if (w.descend_map_set && node instanceof Map) {
		const entries = [...node.entries()];
		const last = entries.length - 1;
		for (let index = 0; index <= last; index++) {
			if (!w.alive && !immutable) break;
			const [key, val] = entries[index];
			path.push(key);
			if (pre !== void 0) pre(ctx, val, key);
			const child = walker(val);
			if (immutable && child.node !== val) node.set(key, child.node);
			child.isLast = index === last;
			child.isFirst = index === 0;
			if (post !== void 0) post(ctx, child);
			path.pop();
		}
		return;
	}
	if (w.descend_map_set && node instanceof Set) {
		const vals = [...node];
		const last = vals.length - 1;
		for (let index = 0; index <= last; index++) {
			if (!w.alive && !immutable) break;
			path.push(index);
			if (pre !== void 0) pre(ctx, vals[index], index);
			const child = walker(vals[index]);
			if (immutable && child.node !== vals[index]) {
				node.delete(vals[index]);
				node.add(child.node);
			}
			child.isLast = index === last;
			child.isFirst = index === 0;
			if (post !== void 0) post(ctx, child);
			path.pop();
		}
		return;
	}
	const keys = ctx.keys;
	const node_is_array = is_array(node);
	let last = keys.length - 1;
	for (let index = 0; index <= last; index++) {
		if (!w.alive && !immutable) break;
		const key = keys[index];
		const childVal = node[key];
		const len_before = node_is_array ? node.length : 0;
		path.push(key);
		if (pre !== void 0) pre(ctx, childVal, key);
		const child = walker(childVal);
		if (immutable && !child.removed && node[key] !== child.node) {
			if (fresh || has_own_property.call(node, key) && !is_non_writable(node, key)) safe_set(node, key, child.node);
		}
		child.isLast = index === last;
		child.isFirst = index === 0;
		if (post !== void 0) post(ctx, child);
		path.pop();
		if (node_is_array && node.length < len_before) {
			index--;
			last--;
		}
	}
}
function update_state(ctx) {
	const node = ctx.node;
	if (typeof node === "object" && node !== null) {
		if (!ctx.keys || ctx.node_ !== node) if (ctx.w.descend_map_set && node instanceof Map) ctx.keys = map_set_child_keys(node);
		else if (ctx.w.descend_map_set && node instanceof Set) ctx.keys = map_set_child_keys(node);
		else {
			const ks = ctx.w.iter(node);
			ctx.keys = is_array(node) ? array_keys(node, ks) : ks;
		}
		ctx.isLeaf = ctx.keys.length === 0;
	} else {
		ctx.isLeaf = true;
		ctx.keys = null;
	}
}
function initial_keys(w, node0, iter) {
	if (w.descend_map_set && node0 instanceof Map) return map_set_child_keys(node0);
	if (w.descend_map_set && node0 instanceof Set) return map_set_child_keys(node0);
	const keys = iter(node0);
	return is_array(node0) ? array_keys(node0, keys) : keys;
}
/**
* Depth-first walk; {@link forEach} and {@link map} use this internally.
*
* @example
* ```js
* import { walk } from 'neotraverse/modern';
* walk({ a: { b: 1 } }, (ctx) => {
*   if (ctx.path.join('.') === 'a.b') ctx.update(2);
* });
* // => { a: { b: 2 } }
* ```
*
* @see https://neotraverse.puruvj.dev/guide/api/walk#t-walk
*/
function walk(root, cb, options = empty_null) {
	const w = make_walk_state(options);
	const { immutable, max_depth, parents, iter } = w;
	const walker = (node_) => {
		assert_within_depth(w.path.length, max_depth);
		const node0 = immutable ? copy(node_, options) : node_;
		const ctx = new WalkContext(w, node_, node0);
		if (!w.alive) return ctx;
		const node0_is_obj = typeof node0 === "object" && node0 !== null;
		if (node0_is_obj) {
			const keys0 = initial_keys(w, node0, iter);
			ctx.keys = keys0;
			ctx.isLeaf = keys0.length === 0;
			for (let i = 0; i < parents.length; i++) if (parents[i].node_ === node_) {
				ctx.circular = parents[i];
				break;
			}
		} else ctx.isLeaf = true;
		const ret = cb(ctx, node0);
		if (ret !== void 0) ctx.update(ret);
		const mods = ctx.mods;
		if (mods !== null && mods.before !== void 0) mods.before(ctx, ctx.node);
		if (!ctx.keep_going) return ctx;
		const node = ctx.node;
		const fresh = node === node0;
		if ((fresh ? node0_is_obj : typeof node === "object" && node !== null) && ctx.circular === void 0) {
			parents.push(ctx);
			if (!fresh) update_state(ctx);
			descend_children(w, ctx, node, walker, immutable, mods, fresh);
			parents.pop();
		}
		if (mods !== null && mods.after !== void 0) mods.after(ctx, ctx.node);
		return ctx;
	};
	return walker(root).node;
}
/**
* @example
* ```js
* import { forEach } from 'neotraverse/modern';
* forEach([5, -3], (ctx, x) => { if (x < 0) ctx.update(x + 128); });
* // => [5, 125]
* ```
*
* @see https://neotraverse.puruvj.dev/guide/api/core#t-forEach
*/
function forEach(obj, cb, options) {
	return walk(obj, cb, options);
}
//#endregion
//#region node_modules/astro/dist/assets/runtime.js
function createSvgComponent({ meta, attributes, children, styles }) {
	const hasStyles = styles.length > 0;
	const Component = createComponent({
		async factory(result, props) {
			const normalizedProps = normalizeProps(attributes, props);
			if (hasStyles && result.cspDestination) for (const style of styles) {
				const hash = await generateCspDigest(style, result.cspAlgorithm);
				result._metadata.extraStyleHashes.push(hash);
			}
			return renderTemplate`<svg${spreadAttributes(normalizedProps)}>${unescapeHTML(children)}</svg>`;
		},
		propagation: hasStyles ? "self" : "none"
	});
	Object.defineProperty(Component, "toJSON", {
		value: () => meta,
		enumerable: false
	});
	return Object.assign(Component, meta);
}
var ATTRS_TO_DROP = [
	"xmlns",
	"xmlns:xlink",
	"version"
];
var DEFAULT_ATTRS = {};
function dropAttributes(attributes) {
	for (const attr of ATTRS_TO_DROP) delete attributes[attr];
	return attributes;
}
function normalizeProps(attributes, props) {
	return dropAttributes({
		...DEFAULT_ATTRS,
		...attributes,
		...props
	});
}
var CONTENT_IMAGE_FLAG = "astroContentImageFlag";
var DATA_STORE_VIRTUAL_ID = "astro:data-layer-content";
var IMAGE_IMPORT_PREFIX = "__ASTRO_IMAGE_";
`${DATA_STORE_VIRTUAL_ID}`;
//#endregion
//#region node_modules/astro/dist/assets/utils/resolveImports.js
function imageSrcToImportId(imageSrc, filePath) {
	imageSrc = removeBase(imageSrc, IMAGE_IMPORT_PREFIX);
	if (isRemotePath(imageSrc)) return;
	const ext = imageSrc.split(".").at(-1)?.toLowerCase();
	if (!ext || !VALID_INPUT_FORMATS.includes(ext)) return;
	const params = new URLSearchParams(CONTENT_IMAGE_FLAG);
	if (filePath) params.set("importer", filePath);
	return `${imageSrc}?${params.toString()}`;
}
//#endregion
//#region node_modules/astro/dist/content/data-store-source.js
var InMemorySource = class {
	#store;
	constructor(store) {
		this.#store = store;
	}
	hasCollection(collection) {
		return this.#store.hasCollection(collection);
	}
	get(collection, key) {
		return this.#store.get(collection, key);
	}
	entries(collection) {
		return this.#store.entries(collection);
	}
	values(collection) {
		return this.#store.values(collection);
	}
	keys(collection) {
		return this.#store.keys(collection);
	}
	has(collection, key) {
		return this.#store.has(collection, key);
	}
	collections() {
		return this.#store.collections();
	}
};
//#endregion
//#region node_modules/astro/dist/content/data-store.js
var ImmutableDataStore = class ImmutableDataStore {
	_collections = /* @__PURE__ */ new Map();
	constructor() {
		this._collections = /* @__PURE__ */ new Map();
	}
	get(collectionName, key) {
		return this._collections.get(collectionName)?.get(String(key));
	}
	entries(collectionName) {
		return [...(this._collections.get(collectionName) ?? /* @__PURE__ */ new Map()).entries()];
	}
	values(collectionName) {
		return [...(this._collections.get(collectionName) ?? /* @__PURE__ */ new Map()).values()];
	}
	keys(collectionName) {
		return [...(this._collections.get(collectionName) ?? /* @__PURE__ */ new Map()).keys()];
	}
	has(collectionName, key) {
		const collection = this._collections.get(collectionName);
		if (collection) return collection.has(String(key));
		return false;
	}
	hasCollection(collectionName) {
		return this._collections.has(collectionName);
	}
	collections() {
		return this._collections;
	}
	/**
	* Rebuilds a collections map from a chunked-store manifest whose part file
	* names have already been swapped for their contents.
	*
	* Each collection maps to a list of parts. A part is either a raw string
	* (when the store is loaded from disk) or an ESM namespace from a virtual
	* chunk import (`{ default: string }`, when emitted at runtime). A collection's
	* parts are concatenated back into the exact
	* serialized string, then parsed with devalue. This is the inverse of
	* {@link import('./data-store-writer.js').ChunkedWriter} and stays free of
	* Node built-ins so it can run at runtime.
	*/
	static manifestToMap(manifest) {
		const collections = /* @__PURE__ */ new Map();
		for (const [collectionName, parts] of Object.entries(manifest)) {
			let stringified = "";
			for (const part of parts) stringified += typeof part === "string" ? part : part.default;
			const entries = devalue.parse(stringified);
			collections.set(collectionName, entries);
		}
		return collections;
	}
	/**
	* Attempts to load a DataStore from the virtual module.
	* This only works in Vite.
	*/
	static async fromModule() {
		try {
			const data = await import("./_astro_data-layer-content_BzqJ-QqE.mjs");
			if (data.default instanceof Map) return ImmutableDataStore.fromMap(data.default);
			if (Array.isArray(data.default)) {
				const map2 = devalue.unflatten(data.default);
				return ImmutableDataStore.fromMap(map2);
			}
			const map = ImmutableDataStore.manifestToMap(data.default);
			return ImmutableDataStore.fromMap(map);
		} catch {}
		return new ImmutableDataStore();
	}
	static async fromMap(data) {
		const store = new ImmutableDataStore();
		store._collections = data;
		return store;
	}
};
function dataStoreSingleton() {
	let instance = void 0;
	return {
		get: async () => {
			if (!instance) instance = ImmutableDataStore.fromModule().then((store) => new InMemorySource(store));
			return instance;
		},
		set: (store) => {
			instance = new InMemorySource(store);
		}
	};
}
var globalDataStore = dataStoreSingleton();
//#endregion
//#region node_modules/astro/dist/content/loaders/errors.js
function formatZodError(error) {
	return error.issues.map((issue) => `  **${issue.path.join(".")}**: ${issue.message}`);
}
var LiveCollectionError = class LiveCollectionError extends Error {
	collection;
	message;
	cause;
	constructor(collection, message, cause) {
		super(message);
		this.collection = collection;
		this.message = message;
		this.cause = cause;
		this.name = "LiveCollectionError";
		if (cause?.stack) this.stack = cause.stack;
	}
	static is(error) {
		return error instanceof LiveCollectionError;
	}
};
var LiveEntryNotFoundError = class extends LiveCollectionError {
	constructor(collection, entryFilter) {
		super(collection, `Entry ${collection} \u2192 ${typeof entryFilter === "string" ? entryFilter : JSON.stringify(entryFilter)} was not found.`);
		this.name = "LiveEntryNotFoundError";
	}
	static is(error) {
		return error?.name === "LiveEntryNotFoundError";
	}
};
var LiveCollectionValidationError = class extends LiveCollectionError {
	constructor(collection, entryId, error) {
		super(collection, [
			`**${collection} \u2192 ${entryId}** data does not match the collection schema.
`,
			...formatZodError(error),
			""
		].join("\n"));
		this.name = "LiveCollectionValidationError";
	}
	static is(error) {
		return error?.name === "LiveCollectionValidationError";
	}
};
var LiveCollectionCacheHintError = class extends LiveCollectionError {
	constructor(collection, entryId, error) {
		super(collection, [
			`**${String(collection)}${entryId ? ` \u2192 ${String(entryId)}` : ""}** returned an invalid cache hint.
`,
			...formatZodError(error),
			""
		].join("\n"));
		this.name = "LiveCollectionCacheHintError";
	}
	static is(error) {
		return error?.name === "LiveCollectionCacheHintError";
	}
};
//#endregion
//#region node_modules/astro/dist/content/runtime.js
var cacheHintSchema = z.object({
	tags: z.array(z.string()).optional(),
	lastModified: z.date().optional()
});
async function parseLiveEntry(entry, schema, collection) {
	try {
		const parsed = await z.safeParseAsync(schema, entry.data);
		if (!parsed.success) return { error: new LiveCollectionValidationError(collection, entry.id, parsed.error) };
		if (entry.cacheHint) {
			const cacheHint = cacheHintSchema.safeParse(entry.cacheHint);
			if (!cacheHint.success) return { error: new LiveCollectionCacheHintError(collection, entry.id, cacheHint.error) };
			entry.cacheHint = cacheHint.data;
		}
		return { entry: {
			...entry,
			data: parsed.data
		} };
	} catch (error) {
		return { error: new LiveCollectionError(collection, `Unexpected error parsing entry ${entry.id} in collection ${collection}`, error) };
	}
}
function createGetCollection({ liveCollections }) {
	return async function getCollection(collection, filter) {
		if (collection in liveCollections) throw new AstroError({
			...UnknownContentCollectionError,
			message: `Collection "${collection}" is a live collection. Use getLiveCollection() instead of getCollection().`
		});
		const hasFilter = typeof filter === "function";
		const store = await globalDataStore.get();
		if (await store.hasCollection(collection)) {
			const { default: imageAssetMap } = await import("./content-assets_DXqEyLLP.mjs");
			const result = [];
			for (const rawEntry of await store.values(collection)) {
				const data = updateImageReferencesInData(rawEntry.data, rawEntry.filePath, imageAssetMap);
				let entry = {
					...rawEntry,
					data,
					collection
				};
				if (hasFilter && !filter(entry)) continue;
				result.push(entry);
			}
			return result;
		} else {
			console.warn(`The collection ${JSON.stringify(collection)} does not exist or is empty. Please check your content config file for errors.`);
			return [];
		}
	};
}
function createGetEntry({ liveCollections }) {
	return async function getEntry(collectionOrLookupObject, lookup) {
		let collection, lookupId;
		if (typeof collectionOrLookupObject === "string") {
			collection = collectionOrLookupObject;
			if (!lookup) throw new AstroError({
				...UnknownContentCollectionError,
				message: "`getEntry()` requires an entry identifier as the second argument."
			});
			lookupId = lookup;
		} else {
			collection = collectionOrLookupObject.collection;
			lookupId = "id" in collectionOrLookupObject ? collectionOrLookupObject.id : collectionOrLookupObject.slug;
		}
		if (collection in liveCollections) throw new AstroError({
			...UnknownContentCollectionError,
			message: `Collection "${collection}" is a live collection. Use getLiveEntry() instead of getEntry().`
		});
		if (typeof lookupId === "object") throw new AstroError({
			...UnknownContentCollectionError,
			message: `The entry identifier must be a string. Received object.`
		});
		const store = await globalDataStore.get();
		if (await store.hasCollection(collection)) {
			const entry = await store.get(collection, lookupId);
			if (!entry) {
				console.warn(`Entry ${collection} → ${lookupId} was not found.`);
				return;
			}
			const { default: imageAssetMap } = await import("./content-assets_DXqEyLLP.mjs");
			const data = updateImageReferencesInData(entry.data, entry.filePath, imageAssetMap);
			const result = {
				...entry,
				data,
				collection
			};
			warnForPropertyAccess(result.data, "slug", `[content] Attempted to access deprecated property on "${collection}" entry.
The "slug" property is no longer automatically added to entries. Please use the "id" property instead.`);
			warnForPropertyAccess(result, "render", `[content] Invalid attempt to access "render()" method on "${collection}" entry.
To render an entry, use "render(entry)" from "astro:content".`);
			return result;
		}
	};
}
function warnForPropertyAccess(entry, prop, message) {
	if (!(prop in entry)) {
		let _value = void 0;
		Object.defineProperty(entry, prop, {
			get() {
				if (_value === void 0) console.error(message);
				return _value;
			},
			set(v) {
				_value = v;
			},
			enumerable: false
		});
	}
}
function createGetLiveCollection({ liveCollections }) {
	return async function getLiveCollection(collection, filter) {
		if (!(collection in liveCollections)) return { error: new LiveCollectionError(collection, `Collection "${collection}" is not a live collection. Use getCollection() instead of getLiveCollection() to load regular content collections.`) };
		try {
			const context = {
				filter,
				collection
			};
			const response = await liveCollections[collection].loader?.loadCollection?.(context);
			if (response && "error" in response) return { error: response.error };
			const { schema } = liveCollections[collection];
			let processedEntries = response.entries;
			if (schema) {
				const entryResults = await Promise.all(response.entries.map((entry) => parseLiveEntry(entry, schema, collection)));
				for (const result of entryResults) if (result.error) return { error: result.error };
				processedEntries = entryResults.map((result) => result.entry);
			}
			let cacheHint = response.cacheHint;
			if (cacheHint) {
				const cacheHintResult = cacheHintSchema.safeParse(cacheHint);
				if (!cacheHintResult.success) return { error: new LiveCollectionCacheHintError(collection, void 0, cacheHintResult.error) };
				cacheHint = cacheHintResult.data;
			}
			if (processedEntries.length > 0) {
				const entryTags = /* @__PURE__ */ new Set();
				let latestModified;
				for (const entry of processedEntries) if (entry.cacheHint) {
					if (entry.cacheHint.tags) entry.cacheHint.tags.forEach((tag) => entryTags.add(tag));
					if (entry.cacheHint.lastModified instanceof Date) {
						if (latestModified === void 0 || entry.cacheHint.lastModified > latestModified) latestModified = entry.cacheHint.lastModified;
					}
				}
				if (entryTags.size > 0 || latestModified || cacheHint) {
					const mergedCacheHint = {};
					if (cacheHint?.tags || entryTags.size > 0) mergedCacheHint.tags = [.../* @__PURE__ */ new Set([...cacheHint?.tags || [], ...entryTags])];
					if (cacheHint?.lastModified && latestModified) mergedCacheHint.lastModified = cacheHint.lastModified > latestModified ? cacheHint.lastModified : latestModified;
					else if (cacheHint?.lastModified || latestModified) mergedCacheHint.lastModified = cacheHint?.lastModified ?? latestModified;
					cacheHint = mergedCacheHint;
				}
			}
			return {
				entries: processedEntries,
				cacheHint
			};
		} catch (error) {
			return { error: new LiveCollectionError(collection, `Unexpected error loading collection ${collection}${error instanceof Error ? `: ${error.message}` : ""}`, error) };
		}
	};
}
function createGetLiveEntry({ liveCollections }) {
	return async function getLiveEntry(collection, lookup) {
		if (!(collection in liveCollections)) return { error: new LiveCollectionError(collection, `Collection "${collection}" is not a live collection. Use getCollection() instead of getLiveEntry() to load regular content collections.`) };
		try {
			const lookupObject = {
				filter: typeof lookup === "string" ? { id: lookup } : lookup,
				collection
			};
			let entry = await liveCollections[collection].loader?.loadEntry?.(lookupObject);
			if (entry && "error" in entry) return { error: entry.error };
			if (!entry) return { error: new LiveEntryNotFoundError(collection, lookup) };
			const { schema } = liveCollections[collection];
			if (schema) {
				const result = await parseLiveEntry(entry, schema, collection);
				if (result.error) return { error: result.error };
				entry = result.entry;
			}
			return {
				entry,
				cacheHint: entry.cacheHint
			};
		} catch (error) {
			return { error: new LiveCollectionError(collection, `Unexpected error loading entry ${collection} → ${typeof lookup === "string" ? lookup : JSON.stringify(lookup)}`, error) };
		}
	};
}
function updateImageReferencesInData(data, fileName, imageAssetMap) {
	const copy = structuredClone(data);
	forEach(copy, function(ctx, val) {
		if (typeof val === "string" && val.startsWith("__ASTRO_IMAGE_")) {
			const src = val.replace(IMAGE_IMPORT_PREFIX, "");
			const id = imageSrcToImportId(src, fileName);
			if (!id) {
				ctx.update(src);
				return;
			}
			const imported = imageAssetMap?.get(id);
			if (imported) {
				if (imported.__svgData) {
					const { __svgData: svgData, ...meta } = imported;
					ctx.update(createSvgComponent({
						meta,
						...svgData
					}));
				} else ctx.update(imported);
			} else ctx.update(src);
		}
	});
	return copy;
}
//#endregion
//#region \0astro:content
var liveCollections = {};
var getCollection = createGetCollection({ liveCollections });
createGetEntry({ liveCollections });
createGetLiveCollection({ liveCollections });
createGetLiveEntry({ liveCollections });
//#endregion
//#region \0virtual:starlight/user-config
var user_config_default = {
	"description": "Production documentation for Kéire Engine, its Hub, editor, runtime, scripting, content pipeline, diagnostics, and release workflows.",
	"logo": {
		"src": "../Website/assets/keire.png",
		"alt": "Kéire Engine",
		"replacesTitle": false
	},
	"social": [{
		"icon": "external",
		"label": "Kéire Engine website",
		"href": "/"
	}, {
		"icon": "github",
		"label": "Kéire Engine on GitHub",
		"href": "https://github.com/hcfgod/KeireEngine"
	}],
	"tableOfContents": {
		"minHeadingLevel": 2,
		"maxHeadingLevel": 3
	},
	"editLink": {},
	"sidebar": [
		{
			"label": "Kéire Engine",
			"translations": {},
			"collapsed": false,
			"items": [{
				"label": "Documentation home",
				"translations": {},
				"slug": "docs",
				"attrs": {}
			}]
		},
		{
			"label": "Start here",
			"translations": {},
			"collapsed": false,
			"items": [
				{
					"translations": {},
					"slug": "docs/reference/overview",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/getting-started",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/project-hub",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/project-system",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/project-settings",
					"attrs": {}
				}
			]
		},
		{
			"label": "Engine foundations",
			"translations": {},
			"collapsed": true,
			"items": [
				{
					"translations": {},
					"slug": "docs/reference/architecture",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/runtime-lifecycle",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/ecs-and-components",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/scene-system",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/gameplay-foundations",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/input-system",
					"attrs": {}
				}
			]
		},
		{
			"label": "Editor and authoring",
			"translations": {},
			"collapsed": true,
			"items": [
				{
					"translations": {},
					"slug": "docs/reference/ui-workspace",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/editor-panels",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/scene-authoring",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/asset-browser",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/input-actions-editor",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/input-debugger",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/undo-redo",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/animation-rigging",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/weapon-authoring",
					"attrs": {}
				}
			]
		},
		{
			"label": "Assets, rendering, and builds",
			"translations": {},
			"collapsed": true,
			"items": [
				{
					"translations": {},
					"slug": "docs/reference/asset-runtime",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/asset-pipeline",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/audio-production",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/builtin-meshes",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/rendering",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/shaders-and-materials",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/material-parity-matrix",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/vfx",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/vfx-beyond-parity-roadmap",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/generated/vfx-capabilities",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/visual-authoring-initiatives",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/player-builds",
					"attrs": {}
				}
			]
		},
		{
			"label": "C# scripting",
			"translations": {},
			"collapsed": true,
			"items": [
				{
					"translations": {},
					"slug": "docs/reference/scripting",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/scripting/getting-started",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/scripting/behaviours-and-lifecycle",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/scripting/serialization-and-inspector",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/scripting/entities-components-and-transforms",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/scripting/assets-and-scriptable-objects",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/scripting/gameplay-services",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/scripting/audio",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/scripting/animation",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/scripting/ui-and-events",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/scripting/async-reload-and-diagnostics",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/scripting/api-index",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/managed-scripting",
					"attrs": {}
				}
			]
		},
		{
			"label": "Production and release",
			"translations": {},
			"collapsed": true,
			"items": [
				{
					"translations": {},
					"slug": "docs/reference/profiling",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/performance-gates",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/testing-and-release",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/package-archives",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/asset-packages",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/production-readiness-review",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/maintainability",
					"attrs": {}
				}
			]
		},
		{
			"label": "Diagnostics",
			"translations": {},
			"collapsed": true,
			"items": [
				{
					"translations": {},
					"slug": "docs/reference/diagnostics",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/diagnostics/keire-audio-0001",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/diagnostics/keire-example-0001",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/diagnostics/keire-replay-0001",
					"attrs": {}
				},
				{
					"translations": {},
					"slug": "docs/reference/diagnostics/keire-replay-0002",
					"attrs": {}
				}
			]
		}
	],
	"head": [{
		"tag": "meta",
		"attrs": {
			"property": "og:image",
			"content": "https://keireengine.duckdns.org/assets/hero-cinematic.png"
		}
	}, {
		"tag": "meta",
		"attrs": {
			"name": "theme-color",
			"content": "#050915"
		}
	}],
	"customCss": ["./Source/styles/keire.css"],
	"lastUpdated": false,
	"pagination": true,
	"favicon": {
		"href": "/assets/keire.png",
		"type": "image/png"
	},
	"pagefind": { "ranking": {
		"pageLength": .1,
		"termFrequency": .1,
		"termSaturation": 2,
		"termSimilarity": 9,
		"diacriticSimilarity": .8
	} },
	"components": {
		"Head": "@astrojs/starlight/components/Head.astro",
		"ThemeProvider": "@astrojs/starlight/components/ThemeProvider.astro",
		"SkipLink": "@astrojs/starlight/components/SkipLink.astro",
		"PageFrame": "@astrojs/starlight/components/PageFrame.astro",
		"MobileMenuToggle": "@astrojs/starlight/components/MobileMenuToggle.astro",
		"TwoColumnContent": "@astrojs/starlight/components/TwoColumnContent.astro",
		"Header": "@astrojs/starlight/components/Header.astro",
		"SiteTitle": "@astrojs/starlight/components/SiteTitle.astro",
		"Search": "@astrojs/starlight/components/Search.astro",
		"SocialIcons": "@astrojs/starlight/components/SocialIcons.astro",
		"ThemeSelect": "@astrojs/starlight/components/ThemeSelect.astro",
		"LanguageSelect": "@astrojs/starlight/components/LanguageSelect.astro",
		"Sidebar": "@astrojs/starlight/components/Sidebar.astro",
		"MobileMenuFooter": "@astrojs/starlight/components/MobileMenuFooter.astro",
		"PageSidebar": "@astrojs/starlight/components/PageSidebar.astro",
		"TableOfContents": "@astrojs/starlight/components/TableOfContents.astro",
		"MobileTableOfContents": "@astrojs/starlight/components/MobileTableOfContents.astro",
		"Banner": "@astrojs/starlight/components/Banner.astro",
		"ContentPanel": "@astrojs/starlight/components/ContentPanel.astro",
		"PageTitle": "@astrojs/starlight/components/PageTitle.astro",
		"FallbackContentNotice": "@astrojs/starlight/components/FallbackContentNotice.astro",
		"DraftContentNotice": "@astrojs/starlight/components/DraftContentNotice.astro",
		"Hero": "@astrojs/starlight/components/Hero.astro",
		"MarkdownContent": "@astrojs/starlight/components/MarkdownContent.astro",
		"Footer": "@astrojs/starlight/components/Footer.astro",
		"LastUpdated": "@astrojs/starlight/components/LastUpdated.astro",
		"Pagination": "@astrojs/starlight/components/Pagination.astro",
		"EditLink": "@astrojs/starlight/components/EditLink.astro"
	},
	"titleDelimiter": "—",
	"disable404Route": false,
	"prerender": true,
	"credits": false,
	"routeMiddleware": [],
	"markdown": {
		"headingLinks": true,
		"processedDirs": []
	},
	"title": { "en": "Kéire Engine Docs" },
	"isMultilingual": false,
	"isUsingBuiltInDefaultLocale": true,
	"defaultLocale": {
		"label": "English",
		"lang": "en",
		"dir": "ltr"
	}
};
//#endregion
//#region \0virtual:starlight/project-context
var project_context_default = {
	"build": { "format": "directory" },
	"root": "file:///C:/Users/keith/Desktop/K%C3%A9ireEngine/Services/KeireDistributionService/DocumentationSite/",
	"srcDir": "file:///C:/Users/keith/Desktop/K%C3%A9ireEngine/Services/KeireDistributionService/DocumentationSite/Source/",
	"trailingSlash": "always"
};
//#endregion
//#region \0virtual:starlight/plugin-translations
var plugin_translations_default = {};
//#endregion
//#region node_modules/@astrojs/starlight/schemas/i18n.ts
function builtinI18nSchema() {
	return z.object({
		...z.strictObject({ ...starlightI18nSchema().required().shape }).shape,
		...pagefindI18nSchema().shape,
		...expressiveCodeI18nSchema().shape
	});
}
function starlightI18nSchema() {
	return z.object({
		"skipLink.label": z.string().meta({ description: "Text displayed in the accessible “Skip link” when a keyboard user first tabs into a page." }),
		"search.label": z.string().meta({ description: "Text displayed in the search bar." }),
		"search.ctrlKey": z.string().meta({ description: "Visible representation of the Control key potentially used in the shortcut key to open the search modal." }),
		"search.cancelLabel": z.string().meta({ description: "Text for the “Cancel” button that closes the search modal." }),
		"search.devWarning": z.string().meta({ description: "Warning displayed when opening the Search in a dev environment." }),
		"themeSelect.accessibleLabel": z.string().meta({ description: "Accessible label for the theme selection dropdown." }),
		"themeSelect.dark": z.string().meta({ description: "Name of the dark color theme." }),
		"themeSelect.light": z.string().meta({ description: "Name of the light color theme." }),
		"themeSelect.auto": z.string().meta({ description: "Name of the automatic color theme that syncs with system preferences." }),
		"languageSelect.accessibleLabel": z.string().meta({ description: "Accessible label for the language selection dropdown." }),
		"menuButton.accessibleLabel": z.string().meta({ description: "Accessible label for the mobile menu button." }),
		"sidebarNav.accessibleLabel": z.string().meta({ description: "Accessible label for the main sidebar `<nav>` element to distinguish it from other `<nav>` landmarks on the page." }),
		"tableOfContents.onThisPage": z.string().meta({ description: "Title for the table of contents component." }),
		"tableOfContents.overview": z.string().meta({ description: "Label used for the first link in the table of contents, linking to the page title." }),
		"i18n.untranslatedContent": z.string().meta({ description: "Notice informing users they are on a page that is not yet translated to their language." }),
		"page.editLink": z.string().meta({ description: "Text for the link to edit a page." }),
		"page.lastUpdated": z.string().meta({ description: "Text displayed in front of the last updated date in the page footer." }),
		"page.previousLink": z.string().meta({ description: "Label shown on the “previous page” pagination arrow in the page footer." }),
		"page.nextLink": z.string().meta({ description: "Label shown on the “next page” pagination arrow in the page footer." }),
		"page.draft": z.string().meta({ description: "Development-only notice informing users they are on a page that is a draft which will not be included in production builds." }),
		"404.text": z.string().meta({ description: "Text shown on Starlight’s default 404 page" }),
		"aside.tip": z.string().meta({ description: "Text shown on the tip aside variant" }),
		"aside.note": z.string().meta({ description: "Text shown on the note aside variant" }),
		"aside.caution": z.string().meta({ description: "Text shown on the warning aside variant" }),
		"aside.danger": z.string().meta({ description: "Text shown on the danger aside variant" }),
		"fileTree.directory": z.string().meta({ description: "Label for the directory icon in the file tree component." }),
		"builtWithStarlight.label": z.string().meta({ description: "Label for the “Built with Starlight” badge optionally displayed in the site footer." }),
		"heading.anchorLabel": z.string().meta({ description: "Label for anchor links in Markdown content." })
	}).partial();
}
function pagefindI18nSchema() {
	return z.object({
		"pagefind.clear_search": z.string().meta({ description: "Pagefind UI translation. English default value: `\"Clear\"`. See https://pagefind.app/docs/ui/#translations" }),
		"pagefind.load_more": z.string().meta({ description: "Pagefind UI translation. English default value: `\"Load more results\"`. See https://pagefind.app/docs/ui/#translations" }),
		"pagefind.search_label": z.string().meta({ description: "Pagefind UI translation. English default value: `\"Search this site\"`. See https://pagefind.app/docs/ui/#translations" }),
		"pagefind.filters_label": z.string().meta({ description: "Pagefind UI translation. English default value: `\"Filters\"`. See https://pagefind.app/docs/ui/#translations" }),
		"pagefind.zero_results": z.string().meta({ description: "Pagefind UI translation. English default value: `\"No results for [SEARCH_TERM]\"`. See https://pagefind.app/docs/ui/#translations" }),
		"pagefind.many_results": z.string().meta({ description: "Pagefind UI translation. English default value: `\"[COUNT] results for [SEARCH_TERM]\"`. See https://pagefind.app/docs/ui/#translations" }),
		"pagefind.one_result": z.string().meta({ description: "Pagefind UI translation. English default value: `\"[COUNT] result for [SEARCH_TERM]\"`. See https://pagefind.app/docs/ui/#translations" }),
		"pagefind.alt_search": z.string().meta({ description: "Pagefind UI translation. English default value: `\"No results for [SEARCH_TERM]. Showing results for [DIFFERENT_TERM] instead\"`. See https://pagefind.app/docs/ui/#translations" }),
		"pagefind.search_suggestion": z.string().meta({ description: "Pagefind UI translation. English default value: `\"No results for [SEARCH_TERM]. Try one of the following searches:\"`. See https://pagefind.app/docs/ui/#translations" }),
		"pagefind.searching": z.string().meta({ description: "Pagefind UI translation. English default value: `\"Searching for [SEARCH_TERM]...\"`. See https://pagefind.app/docs/ui/#translations" })
	}).partial();
}
function expressiveCodeI18nSchema() {
	return z.object({
		"expressiveCode.copyButtonCopied": z.string().meta({ description: "Expressive Code UI translation. English default value: `\"Copied!\"`" }),
		"expressiveCode.copyButtonTooltip": z.string().meta({ description: "Expressive Code UI translation. English default value: `\"Copy to clipboard\"`" }),
		"expressiveCode.terminalWindowFallbackTitle": z.string().meta({ description: "Expressive Code UI translation. English default value: `\"Terminal window\"`" })
	}).partial();
}
//#endregion
//#region node_modules/@astrojs/starlight/translations/cs.json
var cs_default = {
	"skipLink.label": "Přeskočit na obsah",
	"search.label": "Hledat",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Zrušit",
	"search.devWarning": "Vyhledávání je dostupné pouze v produkčních sestaveních. \nZkuste sestavit a zobrazit náhled webu a otestovat jej lokálně.",
	"themeSelect.accessibleLabel": "Vyberte motiv",
	"themeSelect.dark": "Tmavý",
	"themeSelect.light": "Světlý",
	"themeSelect.auto": "Auto",
	"languageSelect.accessibleLabel": "Vyberte jazyk",
	"menuButton.accessibleLabel": "Nabídka",
	"sidebarNav.accessibleLabel": "Hlavní",
	"tableOfContents.onThisPage": "Na této stránce",
	"tableOfContents.overview": "Přehled",
	"i18n.untranslatedContent": "Tento obsah zatím není dostupný ve vašem jazyce.",
	"page.editLink": "Upravit stránku",
	"page.lastUpdated": "Aktualizováno:",
	"page.previousLink": "Předchozí",
	"page.nextLink": "Další",
	"page.draft": "Tento obsah je koncept a nebude zahrnutý v produkčním sestavení.",
	"404.text": "Stránka nenalezena. Zkontrolujte adresu nebo zkuste použít vyhledávač.",
	"aside.note": "Poznámka",
	"aside.tip": "Tip",
	"aside.caution": "Upozornění",
	"aside.danger": "Nebezpečí",
	"fileTree.directory": "Adresář",
	"builtWithStarlight.label": "Postaveno se Starlight",
	"expressiveCode.copyButtonCopied": "Zkopírováno!",
	"expressiveCode.copyButtonTooltip": "Kopírovat do schránky",
	"expressiveCode.terminalWindowFallbackTitle": "Terminál",
	"pagefind.clear_search": "Vyčistit",
	"pagefind.load_more": "Načíst další výsledky",
	"pagefind.search_label": "Vyhledat stránku",
	"pagefind.filters_label": "Filtry",
	"pagefind.zero_results": "Žádný výsledek pro: [SEARCH_TERM]",
	"pagefind.many_results": "počet výsledků: [COUNT] pro: [SEARCH_TERM]",
	"pagefind.one_result": "[COUNT] výsledek pro: [SEARCH_TERM]",
	"pagefind.alt_search": "Žádné výsledky pro [SEARCH_TERM]. Namísto toho zobrazuji výsledky pro: [DIFFERENT_TERM]",
	"pagefind.search_suggestion": "Žádný výsledek pro [SEARCH_TERM]. Zkus nějaké z těchto hledání:",
	"pagefind.searching": "Hledám [SEARCH_TERM]...",
	"heading.anchorLabel": "Sekce “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/en.json
var en_default = {
	"skipLink.label": "Skip to content",
	"search.label": "Search",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Cancel",
	"search.devWarning": "Search is only available in production builds. \nTry building and previewing the site to test it out locally.",
	"themeSelect.accessibleLabel": "Select theme",
	"themeSelect.dark": "Dark",
	"themeSelect.light": "Light",
	"themeSelect.auto": "Auto",
	"languageSelect.accessibleLabel": "Select language",
	"menuButton.accessibleLabel": "Menu",
	"sidebarNav.accessibleLabel": "Main",
	"tableOfContents.onThisPage": "On this page",
	"tableOfContents.overview": "Overview",
	"i18n.untranslatedContent": "This content is not available in your language yet.",
	"page.editLink": "Edit page",
	"page.lastUpdated": "Last updated:",
	"page.previousLink": "Previous",
	"page.nextLink": "Next",
	"page.draft": "This content is a draft and will not be included in production builds.",
	"404.text": "Page not found. Check the URL or try using the search bar.",
	"aside.note": "Note",
	"aside.tip": "Tip",
	"aside.caution": "Caution",
	"aside.danger": "Danger",
	"fileTree.directory": "Directory",
	"builtWithStarlight.label": "Built with Starlight",
	"heading.anchorLabel": "Section titled “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/es.json
var es_default = {
	"skipLink.label": "Ir al contenido",
	"search.label": "Buscar",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Cancelar",
	"search.devWarning": "La búsqueda solo está disponible en las versiones de producción.  \nIntenta construir y previsualizar el sitio para probarlo localmente.",
	"themeSelect.accessibleLabel": "Seleccionar tema",
	"themeSelect.dark": "Oscuro",
	"themeSelect.light": "Claro",
	"themeSelect.auto": "Automático",
	"languageSelect.accessibleLabel": "Seleccionar idioma",
	"menuButton.accessibleLabel": "Menú",
	"sidebarNav.accessibleLabel": "Primario",
	"tableOfContents.onThisPage": "En esta página",
	"tableOfContents.overview": "Sinopsis",
	"i18n.untranslatedContent": "Esta página aún no está disponible en tu idioma.",
	"page.editLink": "Edita esta página",
	"page.lastUpdated": "Última actualización:",
	"page.previousLink": "Página anterior",
	"page.nextLink": "Siguiente página",
	"page.draft": "Este contenido es un borrador y no se incluirá en las versiones de producción.",
	"404.text": "Página no encontrada. Verifica la URL o intenta usar la barra de búsqueda.",
	"aside.note": "Nota",
	"aside.tip": "Consejo",
	"aside.caution": "Precaución",
	"aside.danger": "Peligro",
	"expressiveCode.copyButtonCopied": "¡Copiado!",
	"expressiveCode.copyButtonTooltip": "Copiar al portapapeles",
	"expressiveCode.terminalWindowFallbackTitle": "Ventana de terminal",
	"fileTree.directory": "Directorio",
	"builtWithStarlight.label": "Hecho con Starlight",
	"pagefind.clear_search": "Limpiar",
	"pagefind.load_more": "Cargar más resultados",
	"pagefind.search_label": "Buscar página",
	"pagefind.filters_label": "Filtros",
	"pagefind.zero_results": "Ningún resultado para: [SEARCH_TERM]",
	"pagefind.many_results": "[COUNT] resultados para: [SEARCH_TERM]",
	"pagefind.one_result": "[COUNT] resultado para: [SEARCH_TERM]",
	"pagefind.alt_search": "Ningún resultado para [SEARCH_TERM]. Mostrando resultados para: [DIFFERENT_TERM]",
	"pagefind.search_suggestion": "Ningún resultado para [SEARCH_TERM]. Prueba alguna de estas búsquedas:",
	"pagefind.searching": "Buscando [SEARCH_TERM]...",
	"heading.anchorLabel": "Sección titulada «{{title}}»"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/ca.json
var ca_default = {
	"skipLink.label": "Saltar al contingut",
	"search.label": "Cercar",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Cancel·lar",
	"search.devWarning": "La cerca només està disponible a les versions de producció.  \nProva de construir i previsualitzar el lloc per provar-ho localment.",
	"themeSelect.accessibleLabel": "Seleccionar tema",
	"themeSelect.dark": "Fosc",
	"themeSelect.light": "Clar",
	"themeSelect.auto": "Automàtic",
	"languageSelect.accessibleLabel": "Seleccionar idioma",
	"menuButton.accessibleLabel": "Menú",
	"sidebarNav.accessibleLabel": "Primari",
	"tableOfContents.onThisPage": "En aquesta pàgina",
	"tableOfContents.overview": "Sinopsi",
	"i18n.untranslatedContent": "Aquesta pàgina encara no està disponible en el teu idioma.",
	"page.editLink": "Edita aquesta pàgina",
	"page.lastUpdated": "Última actualització:",
	"page.previousLink": "Pàgina anterior",
	"page.nextLink": "Pàgina següent",
	"page.draft": "Aquest contingut és un esborrany i no s'inclourà en les versions de producció.",
	"404.text": "Pàgina no trobada. Comprova la URL o intenta utilitzar la barra de cerca.",
	"aside.note": "Nota",
	"aside.tip": "Consell",
	"aside.caution": "Precaució",
	"aside.danger": "Perill",
	"expressiveCode.copyButtonCopied": "Copiat!",
	"expressiveCode.copyButtonTooltip": "Copiar al porta-retalls",
	"expressiveCode.terminalWindowFallbackTitle": "Finestra del terminal",
	"fileTree.directory": "Directori",
	"builtWithStarlight.label": "Fet amb Starlight",
	"pagefind.clear_search": "Netejar",
	"pagefind.load_more": "Carregar més resultats",
	"pagefind.search_label": "Cercar pàgina",
	"pagefind.filters_label": "Filtres",
	"pagefind.zero_results": "Cap resultat per a: [SEARCH_TERM]",
	"pagefind.many_results": "[COUNT] resultats per a: [SEARCH_TERM]",
	"pagefind.one_result": "[COUNT] resultat per a: [SEARCH_TERM]",
	"pagefind.alt_search": "Cap resultat per a [SEARCH_TERM]. Mostrant resultats per a: [DIFFERENT_TERM]",
	"pagefind.search_suggestion": "Cap resultat per a [SEARCH_TERM]. Prova alguna d’aquestes cerques:",
	"pagefind.searching": "Cercant [SEARCH_TERM]...",
	"heading.anchorLabel": "Section titled “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/de.json
var de_default = {
	"skipLink.label": "Zum Inhalt springen",
	"search.label": "Suchen",
	"search.ctrlKey": "Strg",
	"search.cancelLabel": "Abbrechen",
	"search.devWarning": "Die Suche ist nur in Produktions-Builds verfügbar. \nVersuche, die Website zu bauen und in der Vorschau anzusehen, um sie lokal zu testen.",
	"themeSelect.accessibleLabel": "Farbschema wählen",
	"themeSelect.dark": "Dunkel",
	"themeSelect.light": "Hell",
	"themeSelect.auto": "System",
	"languageSelect.accessibleLabel": "Sprache wählen",
	"menuButton.accessibleLabel": "Menü",
	"sidebarNav.accessibleLabel": "Hauptnavigation",
	"tableOfContents.onThisPage": "Auf dieser Seite",
	"tableOfContents.overview": "Überblick",
	"i18n.untranslatedContent": "Dieser Inhalt ist noch nicht in deiner Sprache verfügbar.",
	"page.editLink": "Seite bearbeiten",
	"page.lastUpdated": "Zuletzt aktualisiert:",
	"page.previousLink": "Vorherige Seite",
	"page.nextLink": "Nächste Seite",
	"page.draft": "Dieser Inhalt ist ein Entwurf und wird nicht in den Produktions-Builds enthalten sein.",
	"404.text": "Seite nicht gefunden. Überprüfe die URL oder nutze die Suchleiste.",
	"aside.note": "Hinweis",
	"aside.tip": "Tipp",
	"aside.caution": "Achtung",
	"aside.danger": "Gefahr",
	"fileTree.directory": "Ordner",
	"builtWithStarlight.label": "Erstellt mit Starlight",
	"heading.anchorLabel": "Abschnitt betitelt „{{title}}“"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/ja.json
var ja_default = {
	"skipLink.label": "コンテンツにスキップ",
	"search.label": "検索",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "キャンセル",
	"search.devWarning": "検索はプロダクションビルドでのみ利用可能です。\nローカルでテストするには、サイトをビルドしてプレビューしてください。",
	"themeSelect.accessibleLabel": "テーマの選択",
	"themeSelect.dark": "ダーク",
	"themeSelect.light": "ライト",
	"themeSelect.auto": "自動",
	"languageSelect.accessibleLabel": "言語の選択",
	"menuButton.accessibleLabel": "メニュー",
	"sidebarNav.accessibleLabel": "メイン",
	"tableOfContents.onThisPage": "目次",
	"tableOfContents.overview": "概要",
	"i18n.untranslatedContent": "このコンテンツはまだ日本語訳がありません。",
	"page.editLink": "ページを編集",
	"page.lastUpdated": "最終更新日:",
	"page.previousLink": "前へ",
	"page.nextLink": "次へ",
	"page.draft": "このコンテンツは下書きです。プロダクションビルドには含まれません。",
	"404.text": "ページが見つかりません。 URL を確認するか、検索バーを使用してみてください。",
	"aside.note": "ノート",
	"aside.tip": "ヒント",
	"aside.caution": "注意",
	"aside.danger": "危険",
	"fileTree.directory": "ディレクトリ",
	"builtWithStarlight.label": "Starlightで作成",
	"heading.anchorLabel": "Section titled “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/pt.json
var pt_default = {
	"skipLink.label": "Pular para o conteúdo",
	"search.label": "Pesquisar",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Cancelar",
	"search.devWarning": "A pesquisa está disponível apenas em builds em produção. \nTente fazer a build e pré-visualize o site para testar localmente.",
	"themeSelect.accessibleLabel": "Selecionar tema",
	"themeSelect.dark": "Escuro",
	"themeSelect.light": "Claro",
	"themeSelect.auto": "Auto",
	"languageSelect.accessibleLabel": "Selecionar língua",
	"menuButton.accessibleLabel": "Menu",
	"sidebarNav.accessibleLabel": "Principal",
	"tableOfContents.onThisPage": "Nesta página",
	"tableOfContents.overview": "Visão geral",
	"i18n.untranslatedContent": "Este conteúdo não está disponível em sua língua ainda.",
	"page.editLink": "Editar página",
	"page.lastUpdated": "Última atualização:",
	"page.previousLink": "Anterior",
	"page.nextLink": "Próximo",
	"page.draft": "Esse conteúdo é um rascunho e não será incluído em builds de produção.",
	"404.text": "Página não encontrada. Verifique o URL ou tente usar a barra de pesquisa.",
	"aside.note": "Nota",
	"aside.tip": "Dica",
	"aside.caution": "Cuidado",
	"aside.danger": "Perigo",
	"fileTree.directory": "Directory",
	"builtWithStarlight.label": "Feito com Starlight",
	"heading.anchorLabel": "Seção intitulada “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/fa.json
var fa_default = {
	"skipLink.label": "رفتن به محتوا",
	"search.label": "جستجو",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "لغو",
	"search.devWarning": "جستجو تنها در نسخه‌های تولیدی در دسترس است. \nسعی کنید سایت را بسازید و پیش‌نمایش آن را به صورت محلی آزمایش کنید.",
	"themeSelect.accessibleLabel": "انتخاب پوسته",
	"themeSelect.dark": "تیره",
	"themeSelect.light": "روشن",
	"themeSelect.auto": "خودکار",
	"languageSelect.accessibleLabel": "انتخاب زبان",
	"menuButton.accessibleLabel": "منو",
	"sidebarNav.accessibleLabel": "اصلی",
	"tableOfContents.onThisPage": "در این صفحه",
	"tableOfContents.overview": "نگاه کلی",
	"i18n.untranslatedContent": "این محتوا هنوز به زبان شما در دسترس نیست.",
	"page.editLink": "ویرایش صفحه",
	"page.lastUpdated": "آخرین به‌روزرسانی:",
	"page.previousLink": "قبلی",
	"page.nextLink": "بعدی",
	"page.draft": "This content is a draft and will not be included in production builds.",
	"404.text": "صفحه یافت نشد. لطفاً URL را بررسی کنید یا از جستجو استفاده نمایید.",
	"aside.note": "یادداشت",
	"aside.tip": "نکته",
	"aside.caution": "احتیاط",
	"aside.danger": "خطر",
	"fileTree.directory": "فهرست",
	"builtWithStarlight.label": "Built with Starlight",
	"heading.anchorLabel": "Section titled “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/fi.json
var fi_default = {
	"skipLink.label": "Siirry sisältöön",
	"search.label": "Haku",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Peruuta",
	"search.devWarning": "Haku on käytettävissä vain tuotantoversioissa.\nKokeile kääntää ja esikatsella sivustoa paikallisesti testataksesi sitä.",
	"themeSelect.accessibleLabel": "Valitse teema",
	"themeSelect.dark": "Tumma",
	"themeSelect.light": "Vaalea",
	"themeSelect.auto": "Automaattinen",
	"languageSelect.accessibleLabel": "Valitse kieli",
	"menuButton.accessibleLabel": "Valikko",
	"sidebarNav.accessibleLabel": "Päävalikko",
	"tableOfContents.onThisPage": "Tällä sivulla",
	"tableOfContents.overview": "Yleiskatsaus",
	"i18n.untranslatedContent": "Tämä sisältö ei ole vielä saatavilla valitsemallasi kielellä.",
	"page.editLink": "Muokkaa sivua",
	"page.lastUpdated": "Viimeksi päivitetty:",
	"page.previousLink": "Edellinen",
	"page.nextLink": "Seuraava",
	"page.draft": "Tämä sisältö on luonnos eikä sitä sisällytetä tuotantoversioon.",
	"404.text": "Sivua ei löytynyt. Tarkista URL-osoite tai käytä hakupalkkia.",
	"aside.note": "Huomio",
	"aside.tip": "Vinkki",
	"aside.caution": "Varoitus",
	"aside.danger": "Vaara",
	"fileTree.directory": "Hakemisto",
	"builtWithStarlight.label": "Rakennettu Starlightilla",
	"heading.anchorLabel": "Osio nimeltä “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/fr.json
var fr_default = {
	"skipLink.label": "Aller au contenu",
	"search.label": "Rechercher",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Annuler",
	"search.devWarning": "La recherche est disponible uniquement en mode production. \nEssayez de construire puis de prévisualiser votre site pour tester la recherche localement.",
	"themeSelect.accessibleLabel": "Selectionner le thème",
	"themeSelect.dark": "Sombre",
	"themeSelect.light": "Clair",
	"themeSelect.auto": "Auto",
	"languageSelect.accessibleLabel": "Selectionner la langue",
	"menuButton.accessibleLabel": "Menu",
	"sidebarNav.accessibleLabel": "principale",
	"tableOfContents.onThisPage": "Sur cette page",
	"tableOfContents.overview": "Vue d’ensemble",
	"i18n.untranslatedContent": "Ce contenu n’est pas encore disponible dans votre langue.",
	"page.editLink": "Modifier cette page",
	"page.lastUpdated": "Dernière mise à jour :",
	"page.previousLink": "Précédent",
	"page.nextLink": "Suivant",
	"page.draft": "Ce contenu est une ébauche et ne sera pas inclus dans la version de production.",
	"404.text": "Page non trouvée. Vérifiez l’URL ou essayez d’utiliser la barre de recherche.",
	"aside.note": "Note",
	"aside.tip": "Astuce",
	"aside.caution": "Attention",
	"aside.danger": "Danger",
	"expressiveCode.copyButtonCopied": "Copié !",
	"expressiveCode.copyButtonTooltip": "Copier dans le presse-papiers",
	"expressiveCode.terminalWindowFallbackTitle": "Fenêtre de terminal",
	"fileTree.directory": "Répertoire",
	"builtWithStarlight.label": "Créé avec Starlight",
	"heading.anchorLabel": "Section intitulée « {{title}} »"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/gl.json
var gl_default = {
	"skipLink.label": "Ir ao contido",
	"search.label": "Busca",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Deixar",
	"search.devWarning": "A busca só está dispoñible nas versións de producción. \nTrata de construir e ollear o sitio para probalo localmente.",
	"themeSelect.accessibleLabel": "Selecciona tema",
	"themeSelect.dark": "Escuro",
	"themeSelect.light": "Claro",
	"themeSelect.auto": "Auto",
	"languageSelect.accessibleLabel": "Seleciona linguaxe",
	"menuButton.accessibleLabel": "Menú",
	"sidebarNav.accessibleLabel": "Principal",
	"tableOfContents.onThisPage": "Nesta páxina",
	"tableOfContents.overview": "Sinopse",
	"i18n.untranslatedContent": "Este contido aínda non está dispoñible no teu idioma.",
	"page.editLink": "Editar páxina",
	"page.lastUpdated": "Última actualización:",
	"page.previousLink": "Anterior",
	"page.nextLink": "Seguinte",
	"page.draft": "Este contido é un borrador e non se incluirá nas versións de producción.",
	"404.text": "Páxina non atopada. Comproba a URL ou intenta usar a barra de busca.",
	"aside.note": "Nota",
	"aside.tip": "Consello",
	"aside.caution": "Precaución",
	"aside.danger": "Perigo",
	"expressiveCode.copyButtonCopied": "¡Copiado!",
	"expressiveCode.copyButtonTooltip": "Copiar ao portapapeis",
	"expressiveCode.terminalWindowFallbackTitle": "Fiestra do terminal",
	"fileTree.directory": "Directorio",
	"builtWithStarlight.label": "Feito con Starlight",
	"pagefind.clear_search": "Limpar",
	"pagefind.load_more": "Cargar máis resultados",
	"pagefind.search_label": "Buscar páxina",
	"pagefind.filters_label": "Filtros",
	"pagefind.zero_results": "Ningún resultado para: [SEARCH_TERM]",
	"pagefind.many_results": "[COUNT] resultados para: [SEARCH_TERM]",
	"pagefind.one_result": "[COUNT] resultado para: [SEARCH_TERM]",
	"pagefind.alt_search": "Ningún resultado para [SEARCH_TERM]. Amósanse resultados para: [DIFFERENT_TERM]",
	"pagefind.search_suggestion": "Ningún resultado para [SEARCH_TERM]. Proba algunha destas buscas:",
	"pagefind.searching": "Buscando [SEARCH_TERM]...",
	"heading.anchorLabel": "Sección titulada «{{title}}»"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/he.json
var he_default = {
	"skipLink.label": "דלגו לתוכן",
	"search.label": "חיפוש",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "ביטול",
	"search.devWarning": "החיפוש זמין רק בסביבת ייצור. \nנסו לבנות ולהציג תצוגה מקדימה של האתר כדי לבדוק אותו באופן מקומי.",
	"themeSelect.accessibleLabel": "בחרו פרופיל צבע",
	"themeSelect.dark": "כהה",
	"themeSelect.light": "בהיר",
	"themeSelect.auto": "מערכת",
	"languageSelect.accessibleLabel": "בחרו שפה",
	"menuButton.accessibleLabel": "תפריט",
	"sidebarNav.accessibleLabel": "ראשי",
	"tableOfContents.onThisPage": "בדף זה",
	"tableOfContents.overview": "סקירה כללית",
	"i18n.untranslatedContent": "תוכן זה אינו זמין עדיין בשפה שלך.",
	"page.editLink": "ערכו דף",
	"page.lastUpdated": "עדכון אחרון:",
	"page.previousLink": "הקודם",
	"page.nextLink": "הבא",
	"page.draft": "This content is a draft and will not be included in production builds.",
	"404.text": "הדף לא נמצא. אנא בדקו את כתובת האתר או נסו להשתמש בסרגל החיפוש.",
	"aside.note": "Note",
	"aside.tip": "Tip",
	"aside.caution": "Caution",
	"aside.danger": "Danger",
	"fileTree.directory": "Directory",
	"builtWithStarlight.label": "Built with Starlight",
	"heading.anchorLabel": "Section titled “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/id.json
var id_default = {
	"skipLink.label": "Lewati ke konten",
	"search.label": "Pencarian",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Batal",
	"search.devWarning": "Pencarian hanya tersedia pada build produksi. \nLakukan proses build dan pratinjau situs Anda sebelum mencoba di lingkungan lokal.",
	"themeSelect.accessibleLabel": "Pilih tema",
	"themeSelect.dark": "Gelap",
	"themeSelect.light": "Terang",
	"themeSelect.auto": "Otomatis",
	"languageSelect.accessibleLabel": "Pilih Bahasa",
	"menuButton.accessibleLabel": "Menu",
	"sidebarNav.accessibleLabel": "Utama",
	"tableOfContents.onThisPage": "Di halaman ini",
	"tableOfContents.overview": "Ringkasan",
	"i18n.untranslatedContent": "Konten ini belum tersedia dalam bahasa Anda.",
	"page.editLink": "Edit halaman",
	"page.lastUpdated": "Terakhir diperbaharui:",
	"page.previousLink": "Sebelumnya",
	"page.nextLink": "Selanjutnya",
	"page.draft": "This content is a draft and will not be included in production builds.",
	"404.text": "Halaman tidak ditemukan. Cek kembali kolom URL atau gunakan fitur pencarian.",
	"aside.note": "Catatan",
	"aside.tip": "Tips",
	"aside.caution": "Perhatian",
	"aside.danger": "Bahaya",
	"fileTree.directory": "Directory",
	"builtWithStarlight.label": "Built with Starlight",
	"heading.anchorLabel": "Section titled “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/it.json
var it_default = {
	"skipLink.label": "Salta ai contenuti",
	"search.label": "Cerca",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Cancella",
	"search.devWarning": "La ricerca è disponibile solo nelle build di produzione. \nProvare ad eseguire il processo di build e visualizzare la preview del sito per testarlo localmente.",
	"themeSelect.accessibleLabel": "Seleziona tema",
	"themeSelect.dark": "Scuro",
	"themeSelect.light": "Chiaro",
	"themeSelect.auto": "Auto",
	"languageSelect.accessibleLabel": "Seleziona lingua",
	"menuButton.accessibleLabel": "Menu",
	"sidebarNav.accessibleLabel": "Principale",
	"tableOfContents.onThisPage": "In questa pagina",
	"tableOfContents.overview": "Panoramica",
	"i18n.untranslatedContent": "Questi contenuti non sono ancora disponibili nella tua lingua.",
	"page.editLink": "Modifica pagina",
	"page.lastUpdated": "Ultimo aggiornamento:",
	"page.previousLink": "Indietro",
	"page.nextLink": "Avanti",
	"page.draft": "This content is a draft and will not be included in production builds.",
	"404.text": "Pagina non trovata. Verifica l'URL o prova a utilizzare la barra di ricerca.",
	"aside.note": "Nota",
	"aside.tip": "Consiglio",
	"aside.caution": "Attenzione",
	"aside.danger": "Pericolo",
	"fileTree.directory": "Directory",
	"builtWithStarlight.label": "Built with Starlight",
	"heading.anchorLabel": "Sezione intitolata “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/nl.json
var nl_default = {
	"skipLink.label": "Ga naar inhoud",
	"search.label": "Zoeken",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Annuleren",
	"search.devWarning": "Zoeken is alleen beschikbaar tijdens productie. \nProbeer om de site te builden en er een preview van te bekijken om lokaal te testen.",
	"themeSelect.accessibleLabel": "Selecteer thema",
	"themeSelect.dark": "Donker",
	"themeSelect.light": "Licht",
	"themeSelect.auto": "Auto",
	"languageSelect.accessibleLabel": "Selecteer taal",
	"menuButton.accessibleLabel": "Menu",
	"sidebarNav.accessibleLabel": "Hoofdnavigatie",
	"tableOfContents.onThisPage": "Op deze pagina",
	"tableOfContents.overview": "Overzicht",
	"i18n.untranslatedContent": "Deze inhoud is nog niet vertaald.",
	"page.editLink": "Bewerk pagina",
	"page.lastUpdated": "Laatst bewerkt:",
	"page.previousLink": "Vorige",
	"page.nextLink": "Volgende",
	"page.draft": "This content is a draft and will not be included in production builds.",
	"404.text": "Pagina niet gevonden. Controleer de URL of probeer de zoekbalk.",
	"aside.note": "Opmerking",
	"aside.tip": "Tip",
	"aside.caution": "Opgepast",
	"aside.danger": "Gevaar",
	"fileTree.directory": "Directory",
	"builtWithStarlight.label": "Built with Starlight",
	"heading.anchorLabel": "Section titled “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/da.json
var da_default = {
	"skipLink.label": "Gå til indhold",
	"search.label": "Søg",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Annuller",
	"search.devWarning": "Søgning er kun tilgængeligt i produktions versioner. \nPrøv at bygge siden og forhåndsvis den for at teste det lokalt.",
	"themeSelect.accessibleLabel": "Vælg tema",
	"themeSelect.dark": "Mørk",
	"themeSelect.light": "Lys",
	"themeSelect.auto": "Auto",
	"languageSelect.accessibleLabel": "Vælg sprog",
	"menuButton.accessibleLabel": "Menu",
	"sidebarNav.accessibleLabel": "Hovednavigation",
	"tableOfContents.onThisPage": "På denne side",
	"tableOfContents.overview": "Oversigt",
	"i18n.untranslatedContent": "Dette indhold er ikke tilgængeligt i dit sprog endnu.",
	"page.editLink": "Rediger siden",
	"page.lastUpdated": "Sidst opdateret:",
	"page.previousLink": "Forrige",
	"page.nextLink": "Næste",
	"page.draft": "Indholdet er en kladde og vil ikke blive inkluderet i produktions versioner.",
	"404.text": "Siden er ikke fundet. Tjek din URL eller prøv søgelinjen.",
	"aside.note": "Note",
	"aside.tip": "Tip",
	"aside.caution": "Bemærk",
	"aside.danger": "Advarsel",
	"fileTree.directory": "Mappe",
	"builtWithStarlight.label": "Bygget med Starlight",
	"heading.anchorLabel": "Sektion kaldt “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/th.json
var th_default = {
	"skipLink.label": "ข้ามไปยังเนื้อหา",
	"search.label": "ค้นหา",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "ยกเลิก",
	"search.devWarning": "การค้นหาสามารถใช้งานได้ในเฉพาะเวอร์ชันใช้งานจริงเท่านั้น\nโปรดลองบิลด์และดูตัวอย่างเว็บไซต์เพื่อทดสอบฟังก์ชันบนอุปกรณ์ของคุณ",
	"themeSelect.accessibleLabel": "เลือกธีม",
	"themeSelect.dark": "มืด",
	"themeSelect.light": "สว่าง",
	"themeSelect.auto": "อัตโนมัติ",
	"languageSelect.accessibleLabel": "เลือกภาษา",
	"menuButton.accessibleLabel": "เมนู",
	"sidebarNav.accessibleLabel": "หลัก",
	"tableOfContents.onThisPage": "ในหน้านี้",
	"tableOfContents.overview": "ภาพรวม",
	"i18n.untranslatedContent": "เนื้อหานี้ยังไม่มีในภาษาของคุณ",
	"page.editLink": "แก้ไขหน้า",
	"page.lastUpdated": "อัพเดทล่าสุด:",
	"page.previousLink": "ก่อนหน้า",
	"page.nextLink": "ถัดไป",
	"page.draft": "เนื้อหานี้เป็นแบบร่างและจะไม่ถูกใส่ในเวอร์ชันใช้งานจริง",
	"404.text": "ไม่พบหน้า โปรดตรวจสอบ URL หรือลองใช้ฟังก์ชันการค้นหา",
	"aside.note": "หมายเหตุ",
	"aside.tip": "เคล็ดลับ",
	"aside.caution": "คำเตือน",
	"aside.danger": "อันตราย",
	"fileTree.directory": "โฟลเดอร์",
	"builtWithStarlight.label": "ถูกสร้างขึ้นด้วย Starlight",
	"heading.anchorLabel": "หัวข้อที่มีชื่อว่า “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/tr.json
var tr_default = {
	"skipLink.label": "İçeriğe geç",
	"search.label": "Ara",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "İptal",
	"search.devWarning": "Arama yalnızca üretim yapımlarında kullanılabilir. \nYerel olarak denemek için siteyi oluşturmayı ve önizlemeyi deneyin.",
	"themeSelect.accessibleLabel": "Tema seç",
	"themeSelect.dark": "Koyu",
	"themeSelect.light": "Açık",
	"themeSelect.auto": "Otomatik",
	"languageSelect.accessibleLabel": "Dil seçin",
	"menuButton.accessibleLabel": "Menü",
	"sidebarNav.accessibleLabel": "Ana",
	"tableOfContents.onThisPage": "Sayfa içeriği",
	"tableOfContents.overview": "Genel Bakış",
	"i18n.untranslatedContent": "Bu içerik henüz dilinizde mevcut değil.",
	"page.editLink": "Sayfayı düzenle",
	"page.lastUpdated": "Son güncelleme:",
	"page.previousLink": "Önceki",
	"page.nextLink": "Sonraki",
	"page.draft": "Bu içerik bir taslaktır ve üretim yapımlarına dahil edilmeyecektir.",
	"404.text": "Sayfa bulunamadı. URL'yi gözden geçirin veya arama çubuğunu kullanmayı deneyin.",
	"aside.note": "Not",
	"aside.tip": "İpucu",
	"aside.caution": "Dikkat",
	"aside.danger": "Tehlike",
	"fileTree.directory": "Dizin",
	"builtWithStarlight.label": "Starlight ile oluşturuldu",
	"heading.anchorLabel": "Bölüm başlığı “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/ar.json
var ar_default = {
	"skipLink.label": "تخطَّ إلى المحتوى",
	"search.label": "ابحث",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "إلغاء",
	"search.devWarning": "البحث متوفر فقط في بنيات اﻹنتاج. \n جرب بناء المشروع ومعاينته على جهازك",
	"themeSelect.accessibleLabel": "اختر سمة",
	"themeSelect.dark": "داكن",
	"themeSelect.light": "فاتح",
	"themeSelect.auto": "تلقائي",
	"languageSelect.accessibleLabel": "اختر لغة",
	"menuButton.accessibleLabel": "القائمة",
	"sidebarNav.accessibleLabel": "الرئيسية",
	"tableOfContents.onThisPage": "على هذه الصفحة",
	"tableOfContents.overview": "نظرة عامة",
	"i18n.untranslatedContent": "هذا المحتوى غير متوفر بلغتك بعد.",
	"page.editLink": "عدل الصفحة",
	"page.lastUpdated": "آخر تحديث:",
	"page.previousLink": "السابق",
	"page.nextLink": "التالي",
	"page.draft": "هذا المحتوى مجرد مسودة ولن يظهر في بنيات الإنتاج",
	"404.text": "الصفحة غير موجودة. تأكد من الرابط أو ابحث بإستعمال شريط البحث.",
	"aside.note": "ملاحظة",
	"aside.tip": "نصيحة",
	"aside.caution": "تنبيه",
	"aside.danger": "تحذير",
	"fileTree.directory": "Directory",
	"builtWithStarlight.label": "طوِّر بواسطة Starlight",
	"heading.anchorLabel": "Section titled “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/nb.json
var nb_default = {
	"skipLink.label": "Gå til innholdet",
	"search.label": "Søk",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Avbryt",
	"search.devWarning": "Søk er bare tilgjengelig i produksjonsbygg. \nPrøv å bygg siden og forhåndsvis den for å teste det lokalt.",
	"themeSelect.accessibleLabel": "Velg tema",
	"themeSelect.dark": "Mørk",
	"themeSelect.light": "Lys",
	"themeSelect.auto": "Auto",
	"languageSelect.accessibleLabel": "Velg språk",
	"menuButton.accessibleLabel": "Meny",
	"sidebarNav.accessibleLabel": "Hovednavigasjon",
	"tableOfContents.onThisPage": "På denne siden",
	"tableOfContents.overview": "Oversikt",
	"i18n.untranslatedContent": "Dette innholdet er ikke tilgjengelig på ditt språk.",
	"page.editLink": "Rediger side",
	"page.lastUpdated": "Sist oppdatert:",
	"page.previousLink": "Forrige",
	"page.nextLink": "Neste",
	"page.draft": "This content is a draft and will not be included in production builds.",
	"404.text": "Siden ble ikke funnet. Sjekk URL-en eller prøv å bruke søkefeltet.",
	"aside.note": "Merknad",
	"aside.tip": "Tips",
	"aside.caution": "Advarsel",
	"aside.danger": "Fare",
	"fileTree.directory": "Mappe",
	"builtWithStarlight.label": "Laget med Starlight",
	"heading.anchorLabel": "Section titled “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/zh-CN.json
var zh_CN_default = {
	"skipLink.label": "跳转到内容",
	"search.label": "搜索",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "取消",
	"search.devWarning": "搜索仅适用于生产版本。\n尝试构建并预览网站以在本地测试。",
	"themeSelect.accessibleLabel": "选择主题",
	"themeSelect.dark": "深色",
	"themeSelect.light": "浅色",
	"themeSelect.auto": "自动",
	"languageSelect.accessibleLabel": "选择语言",
	"menuButton.accessibleLabel": "菜单",
	"sidebarNav.accessibleLabel": "主要",
	"tableOfContents.onThisPage": "本页内容",
	"tableOfContents.overview": "概述",
	"i18n.untranslatedContent": "此内容尚不支持你的语言。",
	"page.editLink": "编辑此页",
	"page.lastUpdated": "最近更新：",
	"page.previousLink": "上一页",
	"page.nextLink": "下一页",
	"page.draft": "此内容为草稿，不会包含在生产版本中。",
	"404.text": "页面未找到。检查 URL 或尝试使用搜索栏。",
	"aside.note": "注意",
	"aside.tip": "提示",
	"aside.caution": "警告",
	"aside.danger": "危险",
	"fileTree.directory": "文件夹",
	"builtWithStarlight.label": "基于 Starlight 构建",
	"heading.anchorLabel": "Section titled “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/ko.json
var ko_default = {
	"skipLink.label": "콘텐츠로 이동",
	"search.label": "검색",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "취소",
	"search.devWarning": "검색 기능은 프로덕션 빌드에서만 작동합니다. \n로컬에서 테스트하려면 사이트를 빌드하고 미리 보기를 실행하세요.",
	"themeSelect.accessibleLabel": "테마 선택",
	"themeSelect.dark": "어두운 테마",
	"themeSelect.light": "밝은 테마",
	"themeSelect.auto": "자동",
	"languageSelect.accessibleLabel": "언어 선택",
	"menuButton.accessibleLabel": "메뉴",
	"sidebarNav.accessibleLabel": "메인",
	"tableOfContents.onThisPage": "목차",
	"tableOfContents.overview": "개요",
	"i18n.untranslatedContent": "이 콘텐츠는 아직 번역되지 않았습니다.",
	"page.editLink": "페이지 편집",
	"page.lastUpdated": "마지막 업데이트:",
	"page.previousLink": "이전 페이지",
	"page.nextLink": "다음 페이지",
	"page.draft": "이 콘텐츠는 아직 초안 상태이며, 최종 빌드에는 포함되지 않습니다.",
	"404.text": "페이지를 찾을 수 없습니다. URL을 다시 확인해보거나 검색을 사용해보세요.",
	"aside.note": "참고",
	"aside.tip": "팁",
	"aside.caution": "주의",
	"aside.danger": "위험",
	"fileTree.directory": "디렉터리",
	"builtWithStarlight.label": "이 웹사이트는 Starlight로 제작되었습니다.",
	"heading.anchorLabel": "섹션 제목: “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/sv.json
var sv_default = {
	"skipLink.label": "Hoppa till innehåll",
	"search.label": "Sök",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Avbryt",
	"search.devWarning": "Sökfunktionen är endast tillgänglig i produktionsbyggen. \nProva att bygga och förhandsvisa siten för att testa det lokalt.",
	"themeSelect.accessibleLabel": "Välj tema",
	"themeSelect.dark": "Mörkt",
	"themeSelect.light": "Ljust",
	"themeSelect.auto": "Auto",
	"languageSelect.accessibleLabel": "Välj språk",
	"menuButton.accessibleLabel": "Meny",
	"sidebarNav.accessibleLabel": "Huvudmeny",
	"tableOfContents.onThisPage": "På den här sidan",
	"tableOfContents.overview": "Översikt",
	"i18n.untranslatedContent": "Det här innehållet är inte tillgängligt på ditt språk än.",
	"page.editLink": "Redigera sida",
	"page.lastUpdated": "Senast uppdaterad:",
	"page.previousLink": "Föregående",
	"page.nextLink": "Nästa",
	"page.draft": "This content is a draft and will not be included in production builds.",
	"404.text": "Sidan hittades inte. Kontrollera URL:n eller testa att använda sökfältet.",
	"aside.note": "Note",
	"aside.tip": "Tip",
	"aside.caution": "Caution",
	"aside.danger": "Danger",
	"fileTree.directory": "Directory",
	"builtWithStarlight.label": "Built with Starlight",
	"heading.anchorLabel": "Section titled “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/ro.json
var ro_default = {
	"skipLink.label": "Sari la conținut",
	"search.label": "Caută",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Anulează",
	"search.devWarning": "Căutarea este disponibilă numai în versiunea de producție. \nÎncercă să construiești și să previzualizezi site-ul pentru a-l testa local.",
	"themeSelect.accessibleLabel": "Selectează tema",
	"themeSelect.dark": "Întunecată",
	"themeSelect.light": "Deschisă",
	"themeSelect.auto": "Auto",
	"languageSelect.accessibleLabel": "Selectează limba",
	"menuButton.accessibleLabel": "Meniu",
	"sidebarNav.accessibleLabel": "Principal",
	"tableOfContents.onThisPage": "Pe această pagină",
	"tableOfContents.overview": "Cuprins",
	"i18n.untranslatedContent": "Acest conținut nu este încă disponibil în limba selectată.",
	"page.editLink": "Editează pagina",
	"page.lastUpdated": "Ultima actualizare:",
	"page.previousLink": "Pagina precedentă",
	"page.nextLink": "Pagina următoare",
	"page.draft": "Acest conținut este o schiță și nu va fi inclus în versiunile de producție.",
	"404.text": "Pagina nu a fost găsită. Verifică adresa URL sau încercă să folosești bara de căutare.",
	"aside.note": "Mențiune",
	"aside.tip": "Sfat",
	"aside.caution": "Atenție",
	"aside.danger": "Pericol",
	"fileTree.directory": "Director",
	"builtWithStarlight.label": "Creat cu Starlight",
	"heading.anchorLabel": "Secțiune intitulată „{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/ru.json
var ru_default = {
	"skipLink.label": "Перейти к содержимому",
	"search.label": "Поиск",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Отменить",
	"search.devWarning": "Поиск доступен только в продакшен-сборках. \nВыполните сборку и запустите превью, чтобы протестировать поиск локально.",
	"themeSelect.accessibleLabel": "Выберите тему",
	"themeSelect.dark": "Тёмная",
	"themeSelect.light": "Светлая",
	"themeSelect.auto": "Авто",
	"languageSelect.accessibleLabel": "Выберите язык",
	"menuButton.accessibleLabel": "Меню",
	"sidebarNav.accessibleLabel": "Основное",
	"tableOfContents.onThisPage": "На этой странице",
	"tableOfContents.overview": "Обзор",
	"i18n.untranslatedContent": "Это содержимое пока не доступно на вашем языке.",
	"page.editLink": "Редактировать страницу",
	"page.lastUpdated": "Последнее обновление:",
	"page.previousLink": "Предыдущая",
	"page.nextLink": "Следующая",
	"page.draft": "Этот контент является черновиком и не будет добавлен в продакшен-сборки.",
	"404.text": "Страница не найдена. Проверьте URL или используйте поиск по сайту.",
	"aside.note": "Заметка",
	"aside.tip": "Совет",
	"aside.caution": "Осторожно",
	"aside.danger": "Опасно",
	"fileTree.directory": "Директория",
	"expressiveCode.copyButtonCopied": "Скопировано!",
	"expressiveCode.copyButtonTooltip": "Копировать",
	"expressiveCode.terminalWindowFallbackTitle": "Окно терминала",
	"builtWithStarlight.label": "Сделано с помощью Starlight",
	"heading.anchorLabel": "Заголовок раздела «{{title}}»"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/vi.json
var vi_default = {
	"skipLink.label": "Bỏ qua để đến nội dung",
	"search.label": "Tìm kiếm",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Hủy",
	"search.devWarning": "Chức năng tìm kiếm chỉ có sẵn trong các phiên bản thật.\nHãy thử xây dựng và xem trước trang web để kiểm tra.",
	"themeSelect.accessibleLabel": "Chọn giao diện",
	"themeSelect.dark": "Tối",
	"themeSelect.light": "Sáng",
	"themeSelect.auto": "Tự động",
	"languageSelect.accessibleLabel": "Chọn ngôn ngữ",
	"menuButton.accessibleLabel": "Menu",
	"sidebarNav.accessibleLabel": "Chính",
	"tableOfContents.onThisPage": "Trên trang này",
	"tableOfContents.overview": "Tổng quan",
	"i18n.untranslatedContent": "Nội dung này hiện chưa có sẵn bằng ngôn ngữ của bạn.",
	"page.editLink": "Chỉnh sửa trang",
	"page.lastUpdated": "Cập nhật lần cuối:",
	"page.previousLink": "Trước",
	"page.nextLink": "Tiếp",
	"page.draft": "Nội dung này là bản nháp và sẽ không được đưa vào các phiên bản thật.",
	"404.text": "Không tìm thấy trang. Hãy kiểm tra lại URL hoặc thử dùng thanh tìm kiếm.",
	"aside.note": "Ghi chú",
	"aside.tip": "Mẹo",
	"aside.caution": "Chú ý",
	"aside.danger": "Nguy hiểm",
	"fileTree.directory": "Thư mục",
	"builtWithStarlight.label": "Được xây dựng bằng Starlight",
	"heading.anchorLabel": "Phần tiêu đề “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/uk.json
var uk_default = {
	"skipLink.label": "Перейти до вмісту",
	"search.label": "Пошук",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Скасувати",
	"search.devWarning": "Пошук доступний лише у виробничих збірках. \nСпробуйте зібрати та переглянути сайт, щоби протестувати його локально",
	"themeSelect.accessibleLabel": "Обрати тему",
	"themeSelect.dark": "Темна",
	"themeSelect.light": "Світла",
	"themeSelect.auto": "Авто",
	"languageSelect.accessibleLabel": "Обрати мову",
	"menuButton.accessibleLabel": "Меню",
	"sidebarNav.accessibleLabel": "Головне",
	"tableOfContents.onThisPage": "На цій сторінці",
	"tableOfContents.overview": "Огляд",
	"i18n.untranslatedContent": "Цей контент ще не доступний вашою мовою.",
	"page.editLink": "Редагувати сторінку",
	"page.lastUpdated": "Останнє оновлення:",
	"page.previousLink": "Назад",
	"page.nextLink": "Далі",
	"page.draft": "Цей контент є чернеткою і не буде включений до виробничих збірок.",
	"404.text": "Сторінку не знайдено. Перевірте URL або спробуйте скористатися пошуком.",
	"aside.note": "Заувага",
	"aside.tip": "Порада",
	"aside.caution": "Обережно",
	"aside.danger": "Небезпечно",
	"fileTree.directory": "Каталог",
	"builtWithStarlight.label": "Створено з Starlight",
	"heading.anchorLabel": "Section titled “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/hi.json
var hi_default = {
	"skipLink.label": "इसे छोड़कर कंटेंट पर जाएं",
	"search.label": "खोजें",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "रद्द करे",
	"search.devWarning": "खोज केवल उत्पादन बिल्ड में उपलब्ध है। \nस्थानीय स्तर पर परीक्षण करने के लिए साइट बनाए और उसका पूर्वावलोकन करने का प्रयास करें।",
	"themeSelect.accessibleLabel": "थीम चुनें",
	"themeSelect.dark": "अँधेरा",
	"themeSelect.light": "रोशनी",
	"themeSelect.auto": "स्वत",
	"languageSelect.accessibleLabel": "भाषा चुने",
	"menuButton.accessibleLabel": "मेन्यू",
	"sidebarNav.accessibleLabel": "मुख्य",
	"tableOfContents.onThisPage": "इस पृष्ठ पर",
	"tableOfContents.overview": "अवलोकन",
	"i18n.untranslatedContent": "यह कंटेंट अभी तक आपकी भाषा में उपलब्ध नहीं है।",
	"page.editLink": "पृष्ठ संपादित करें",
	"page.lastUpdated": "आखिरी अद्यतन:",
	"page.previousLink": "पिछला",
	"page.nextLink": "अगला",
	"page.draft": "This content is a draft and will not be included in production builds.",
	"404.text": "यह पृष्ठ नहीं मिला। URL जांचें या खोज बार का उपयोग करने का प्रयास करें।",
	"aside.note": "टिप्पणी",
	"aside.tip": "संकेत",
	"aside.caution": "सावधानी",
	"aside.danger": "खतरा",
	"fileTree.directory": "Directory",
	"builtWithStarlight.label": "Starlight द्वारा निर्मित",
	"heading.anchorLabel": "Section titled “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/zh-TW.json
var zh_TW_default = {
	"skipLink.label": "跳到內容",
	"search.label": "搜尋",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "取消",
	"search.devWarning": "正式版本才能使用搜尋功能。\n如要在本地測試，請先建置並預覽網站。",
	"themeSelect.accessibleLabel": "選擇佈景主題",
	"themeSelect.dark": "深色",
	"themeSelect.light": "淺色",
	"themeSelect.auto": "自動",
	"languageSelect.accessibleLabel": "選擇語言",
	"menuButton.accessibleLabel": "選單",
	"sidebarNav.accessibleLabel": "主要",
	"tableOfContents.onThisPage": "本頁內容",
	"tableOfContents.overview": "概述",
	"i18n.untranslatedContent": "本頁內容尚未翻譯。",
	"page.editLink": "編輯頁面",
	"page.lastUpdated": "最後更新於：",
	"page.previousLink": "前一則",
	"page.nextLink": "下一則",
	"page.draft": "This content is a draft and will not be included in production builds.",
	"404.text": "找不到頁面。請檢查網址或改用搜尋功能。",
	"aside.note": "注意",
	"aside.tip": "提示",
	"aside.caution": "警告",
	"aside.danger": "危險",
	"fileTree.directory": "目錄",
	"builtWithStarlight.label": "Built with Starlight",
	"heading.anchorLabel": "Section titled “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/pl.json
var pl_default = {
	"skipLink.label": "Przejdź do głównej zawartości",
	"search.label": "Szukaj",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Anuluj",
	"search.devWarning": "Wyszukiwanie jest dostępne tylko w buildach produkcyjnych. \nSpróbuj zbudować i uruchomić aplikację, aby przetestować lokalnie.",
	"themeSelect.accessibleLabel": "Wybierz motyw",
	"themeSelect.dark": "Ciemny",
	"themeSelect.light": "Jasny",
	"themeSelect.auto": "Auto",
	"languageSelect.accessibleLabel": "Wybierz język",
	"menuButton.accessibleLabel": "Menu",
	"sidebarNav.accessibleLabel": "Główne",
	"tableOfContents.onThisPage": "Na tej stronie",
	"tableOfContents.overview": "Przegląd",
	"i18n.untranslatedContent": "Ta treść nie jest jeszcze dostępna w Twoim języku.",
	"page.editLink": "Edytuj stronę",
	"page.lastUpdated": "Ostatnia aktualizacja:",
	"page.previousLink": "Poprzednia strona",
	"page.nextLink": "Następna strona",
	"page.draft": "This content is a draft and will not be included in production builds.",
	"404.text": "Nie znaleziono. Sprawdź URL lub użyj wyszukiwarki.",
	"aside.note": "Notatka",
	"aside.tip": "Wskazówka",
	"aside.caution": "Uwaga",
	"aside.danger": "Ważne",
	"fileTree.directory": "Folder",
	"expressiveCode.copyButtonCopied": "Skopiowane!",
	"expressiveCode.copyButtonTooltip": "Skopiuj do schowka",
	"expressiveCode.terminalWindowFallbackTitle": "Okno terminala",
	"builtWithStarlight.label": "Built with Starlight",
	"heading.anchorLabel": "Dział zatytułowany „{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/sk.json
var sk_default = {
	"skipLink.label": "Preskočiť na obsah",
	"search.label": "Hľadať",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Zrušiť",
	"search.devWarning": "Vyhľadávanie je dostupné len v produkčných zostaveniach. \nSkúste vytvoriť a zobraziť náhľad stránky lokálne.",
	"themeSelect.accessibleLabel": "Vyberte tému",
	"themeSelect.dark": "Tmavý",
	"themeSelect.light": "Svetlý",
	"themeSelect.auto": "Auto",
	"languageSelect.accessibleLabel": "Vyberte jazyk",
	"menuButton.accessibleLabel": "Menu",
	"sidebarNav.accessibleLabel": "Hlavný",
	"tableOfContents.onThisPage": "Na tejto stránke",
	"tableOfContents.overview": "Prehľad",
	"i18n.untranslatedContent": "Tento obsah zatiaľ nie je dostupný vo vašom jazyku.",
	"page.editLink": "Upraviť stránku",
	"page.lastUpdated": "Posledná aktualizácia:",
	"page.previousLink": "Predchádzajúce",
	"page.nextLink": "Nasledujúce",
	"page.draft": "Tento obsah je koncept a nebude zahrnutý do produkčných zostavení.",
	"404.text": "Stránka nenájdená. Skontrolujte URL alebo skúste použiť vyhľadávacie pole.",
	"aside.note": "Poznámka",
	"aside.tip": "Tip",
	"aside.caution": "Upozornenie",
	"aside.danger": "Nebezpečenstvo",
	"fileTree.directory": "Adresár",
	"builtWithStarlight.label": "Postavené so Starlight",
	"heading.anchorLabel": "Section titled “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/lv.json
var lv_default = {
	"skipLink.label": "Pāriet uz saturu",
	"search.label": "Meklēt",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Atcelt",
	"search.devWarning": "Meklēšana ir pieejama tikai ražošanas kompilācijās. \nMēģiniet kompilēt un priekšskatīt vietni, lai to pārbaudītu lokāli.",
	"themeSelect.accessibleLabel": "Izvēlieties tēmu",
	"themeSelect.dark": "Tumša",
	"themeSelect.light": "Gaiša",
	"themeSelect.auto": "Automātiska",
	"languageSelect.accessibleLabel": "Izvēlieties valodu",
	"menuButton.accessibleLabel": "Izvēlne",
	"sidebarNav.accessibleLabel": "Galvenā",
	"tableOfContents.onThisPage": "Šajā lapā",
	"tableOfContents.overview": "Pārskats",
	"i18n.untranslatedContent": "Šis saturs vēl nav pieejams jūsu valodā.",
	"page.editLink": "Rediģēt lapu",
	"page.lastUpdated": "Pēdējoreiz atjaunināts:",
	"page.previousLink": "Iepriekšējā",
	"page.nextLink": "Nākamā",
	"page.draft": "Šis saturs ir melnraksts un netiks iekļauts ražošanas kompilācijās.",
	"404.text": "Lapa nav atrasta. Pārbaudiet URL vai mēģiniet izmantot meklēšanas joslu.",
	"aside.note": "Piezīme",
	"aside.tip": "Padoms",
	"aside.caution": "Uzmanību",
	"aside.danger": "Bīstamība",
	"fileTree.directory": "Direktorija",
	"builtWithStarlight.label": "Veidots ar Starlight",
	"heading.anchorLabel": "Section titled “{{title}}”"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/hu.json
var hu_default = {
	"skipLink.label": "Tovább a tartalomhoz",
	"search.label": "Keresés",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Mégsem",
	"search.devWarning": "A keresés csak a production build-ekben működik. \nPróbáld meg először buildelni, hogy kipróbálhasd.",
	"themeSelect.accessibleLabel": "Téma választás",
	"themeSelect.dark": "Sötét",
	"themeSelect.light": "Világos",
	"themeSelect.auto": "Auto",
	"languageSelect.accessibleLabel": "Nyelv választása",
	"menuButton.accessibleLabel": "Menü",
	"sidebarNav.accessibleLabel": "Fő",
	"tableOfContents.onThisPage": "Ezen az oldalon",
	"tableOfContents.overview": "Tartalom",
	"i18n.untranslatedContent": "Ez a tartalom még nem érhető el a jelenlegi nyelven.",
	"page.editLink": "Oldal szerkesztése",
	"page.lastUpdated": "Utoljára frissítve:",
	"page.previousLink": "Előző",
	"page.nextLink": "Következő",
	"page.draft": "Ez a tartalom még vázlat, így nem lesz benne a production build-ben.",
	"404.text": "Az oldal nem található. Nézd meg az URL-t vagy használd a keresőt.",
	"aside.note": "Megjegyzés",
	"aside.tip": "Tipp",
	"aside.caution": "Figyelem",
	"aside.danger": "Veszély",
	"fileTree.directory": "Könyvtár",
	"builtWithStarlight.label": "Starlight-tal készítve",
	"heading.anchorLabel": "Szekció neve “{{title}}”",
	"expressiveCode.copyButtonCopied": "Másolva!",
	"expressiveCode.copyButtonTooltip": "Másolás",
	"expressiveCode.terminalWindowFallbackTitle": "Terminál",
	"pagefind.clear_search": "Törlés",
	"pagefind.load_more": "Több találat betöltése",
	"pagefind.search_label": "Keresés ezen az oldalon",
	"pagefind.filters_label": "Szűrők",
	"pagefind.zero_results": "Erre a kifejezésre nincs találat: [SEARCH_TERM]",
	"pagefind.many_results": "[COUNT] találat erre: [SEARCH_TERM]",
	"pagefind.one_result": "[COUNT] találat erre: [SEARCH_TERM]",
	"pagefind.alt_search": "Erre a kifejezésre nincs találat: [SEARCH_TERM]. Találatok mutatása erre: [DIFFERENT_TERM]",
	"pagefind.search_suggestion": "Erre a kifejezésre nincs találat: [SEARCH_TERM]. Próbáld meg ezek közül az egyiket:",
	"pagefind.searching": "Keresés erre: [SEARCH_TERM]..."
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/el.json
var el_default = {
	"skipLink.label": "Μετάβαση στο περιεχόμενο",
	"search.label": "Αναζήτηση",
	"search.ctrlKey": "Ctrl",
	"search.cancelLabel": "Ακύρωση",
	"search.devWarning": "Η αναζήτηση είναι διαθέσιμη μόνο σε builds παραγωγής.\nΔοκίμασε να κάνεις build τον ιστότοπο και να τον προεπισκοπήσεις για να τον ελέγξεις τοπικά.",
	"themeSelect.accessibleLabel": "Επιλογή χρωματικού θέματος",
	"themeSelect.dark": "Σκοτεινό",
	"themeSelect.light": "Φωτεινό",
	"themeSelect.auto": "Σύστημα",
	"languageSelect.accessibleLabel": "Επιλογή γλώσσας",
	"menuButton.accessibleLabel": "Μενού",
	"sidebarNav.accessibleLabel": "Κύρια πλοήγηση",
	"tableOfContents.onThisPage": "Σε αυτή τη σελίδα",
	"tableOfContents.overview": "Επισκόπηση",
	"i18n.untranslatedContent": "Αυτό το περιεχόμενο δεν είναι ακόμη διαθέσιμο στη γλώσσα σου.",
	"page.editLink": "Επεξεργασία σελίδας",
	"page.lastUpdated": "Τελευταία ενημέρωση:",
	"page.previousLink": "Προηγούμενη σελίδα",
	"page.nextLink": "Επόμενη σελίδα",
	"page.draft": "Αυτό το περιεχόμενο είναι πρόχειρο και δεν θα περιλαμβάνεται στα builds παραγωγής.",
	"404.text": "Η σελίδα δεν βρέθηκε. Έλεγξε το URL ή χρησιμοποίησε τη γραμμή αναζήτησης.",
	"aside.note": "Σημείωση",
	"aside.tip": "Συμβουλή",
	"aside.caution": "Προσοχή",
	"aside.danger": "Κίνδυνος",
	"fileTree.directory": "Φάκελος",
	"builtWithStarlight.label": "Δημιουργήθηκε με το Starlight",
	"heading.anchorLabel": "Ενότητα με τίτλο «{{title}}»"
};
//#endregion
//#region node_modules/@astrojs/starlight/translations/index.ts
var { parse: parse$1 } = builtinI18nSchema();
var translations_default = Object.fromEntries(Object.entries({
	cs: cs_default,
	en: en_default,
	es: es_default,
	ca: ca_default,
	de: de_default,
	ja: ja_default,
	pt: pt_default,
	fa: fa_default,
	fi: fi_default,
	fr: fr_default,
	gl: gl_default,
	he: he_default,
	id: id_default,
	it: it_default,
	nl: nl_default,
	da: da_default,
	th: th_default,
	tr: tr_default,
	ar: ar_default,
	nb: nb_default,
	zh: zh_CN_default,
	ko: ko_default,
	sv: sv_default,
	ro: ro_default,
	ru: ru_default,
	vi: vi_default,
	uk: uk_default,
	hi: hi_default,
	"zh-TW": zh_TW_default,
	pl: pl_default,
	sk: sk_default,
	lv: lv_default,
	hu: hu_default,
	el: el_default
}).map(([key, dict]) => [key, parse$1(dict)]));
//#endregion
//#region node_modules/@astrojs/starlight/utils/i18n.ts
/**
* A list of well-known right-to-left languages used as a fallback when determining the text
* direction of a locale is not supported by the `Intl.Locale` API in the current environment.
*
* @see getLocaleDir()
* @see https://en.wikipedia.org/wiki/IETF_language_tag#List_of_common_primary_language_subtags
*/
var wellKnownRTL = [
	"ar",
	"fa",
	"he",
	"prs",
	"ps",
	"syc",
	"ug",
	"ur"
];
/** Information about the built-in default locale used as a fallback when no locales are defined. */
var BuiltInDefaultLocale = {
	...getLocaleInfo("en"),
	lang: "en"
};
/** Returns the locale information such as a label and a direction based on a BCP-47 tag. */
function getLocaleInfo(lang) {
	try {
		const locale = new Intl.Locale(lang);
		const label = new Intl.DisplayNames(locale, { type: "language" }).of(lang);
		if (!label || lang === label) throw new Error("Label not found.");
		return {
			label: label[0]?.toLocaleUpperCase(locale) + label.slice(1),
			dir: getLocaleDir(locale)
		};
	} catch {
		throw new AstroUserError(`Failed to get locale information for the '${lang}' locale.`, "Make sure to provide a valid BCP-47 tags (e.g. en, ar, or zh-CN).");
	}
}
/**
* Returns the direction of the passed locale.
* @see https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Intl/Locale/getTextInfo
*/
function getLocaleDir(locale) {
	if ("textInfo" in locale) return locale.textInfo.direction;
	else if ("getTextInfo" in locale) return locale.getTextInfo().direction;
	return wellKnownRTL.includes(locale.language) ? "rtl" : "ltr";
}
//#endregion
//#region node_modules/@astrojs/starlight/utils/createTranslationSystem.ts
/**
* The namespace for i18next resources used by Starlight.
* All translations handled by Starlight are stored in the same namespace and Starlight always use
* a new instance of i18next configured for this namespace.
*/
var I18nextNamespace = "starlight";
async function createTranslationSystem(config, userTranslations, pluginTranslations = {}) {
	const defaultLocale = config.defaultLocale.lang || config.defaultLocale?.locale || BuiltInDefaultLocale.lang;
	const translations = { [defaultLocale]: buildResources(translations_default[defaultLocale] || translations_default[stripLangRegion(defaultLocale)], pluginTranslations[defaultLocale], userTranslations[defaultLocale]) };
	if (config.locales) for (const locale in config.locales) {
		const lang = localeToLang(locale, config.locales, config.defaultLocale);
		translations[lang] = buildResources(translations_default[lang] || translations_default[stripLangRegion(lang)], pluginTranslations[lang], userTranslations[lang]);
	}
	const i18n = i18next.createInstance();
	await i18n.init({
		resources: translations,
		fallbackLng: config.defaultLocale.lang || config.defaultLocale?.locale || BuiltInDefaultLocale.lang
	});
	/**
	* Generate a utility function that returns UI strings for the given language.
	*
	* Also includes a few utility methods:
	* - `all()` method for getting the entire dictionary.
	* - `exists()` method for checking if a key exists in the dictionary.
	* - `dir()` method for getting the text direction of the locale.
	*
	* @param {string | undefined} [lang]
	* @example
	* const t = useTranslations('en');
	* const label = t('search.label');
	* // => 'Search'
	* const dictionary = t.all();
	* // => { 'skipLink.label': 'Skip to content', 'search.label': 'Search', ... }
	* const exists = t.exists('search.label');
	* // => true
	* const dir = t.dir();
	* // => 'ltr'
	*/
	return (lang) => {
		lang ??= config.defaultLocale?.lang || BuiltInDefaultLocale.lang;
		const t = i18n.getFixedT(lang, I18nextNamespace);
		t.all = () => i18n.getResourceBundle(lang, I18nextNamespace);
		t.exists = ((key, options) => i18n.exists(key, {
			lng: lang,
			ns: I18nextNamespace,
			...options
		}));
		t.dir = (dirLang = lang) => i18n.dir(dirLang);
		return t;
	};
}
/**
* Strips the region subtag from a BCP-47 lang string.
* @param {string} [lang]
* @example
* const lang = stripLangRegion('en-GB'); // => 'en'
*/
function stripLangRegion(lang) {
	return lang.replace(/-[a-zA-Z]{2}/, "");
}
/**
* Get the BCP-47 language tag for the given locale.
* @param locale Locale string or `undefined` for the root locale.
*/
function localeToLang(locale, locales, defaultLocale) {
	const lang = locale ? locales?.[locale]?.lang : locales?.root?.lang;
	const defaultLang = defaultLocale?.lang || defaultLocale?.locale;
	return lang || defaultLang || BuiltInDefaultLocale.lang;
}
/** Build an i18next resources dictionary by layering preferred translation sources. */
function buildResources(...dictionaries) {
	const dictionary = {};
	for (const dict of dictionaries) for (const key in dict) {
		const value = dict[key];
		if (value) dictionary[key] = value;
	}
	return { [I18nextNamespace]: dictionary };
}
//#endregion
//#region node_modules/@astrojs/starlight/utils/collection.ts
function getCollectionPathFromRoot(collection, { root, srcDir }) {
	return (typeof srcDir === "string" ? srcDir : srcDir.pathname).replace(typeof root === "string" ? root : root.pathname, "") + "content/" + collection;
}
//#endregion
//#region node_modules/@astrojs/starlight/utils/path.ts
/** Ensure the passed path does not start with a leading slash. */
function stripLeadingSlash(href) {
	if (href[0] === "/") href = href.slice(1);
	return href;
}
/** Remove the extension from a path. */
function stripExtension(path) {
	const periodIndex = path.lastIndexOf(".");
	return path.slice(0, periodIndex > -1 ? periodIndex : void 0);
}
//#endregion
//#region node_modules/@astrojs/starlight/utils/translations.ts
var i18nCollectionPathFromRoot = getCollectionPathFromRoot("i18n", project_context_default);
/** Get all translation data from the i18n collection, keyed by `lang`, which are BCP-47 language tags. */
async function loadTranslations() {
	const warn = console.warn;
	console.warn = () => {};
	const userTranslations = Object.fromEntries((await getCollection("i18n")).map(({ id, data, filePath }) => {
		return [!filePath ? id : stripExtension(stripLeadingSlash(filePath.replace(i18nCollectionPathFromRoot, ""))), data];
	}));
	console.warn = warn;
	return userTranslations;
}
/**
* Generate a utility function that returns UI strings for the given language.
* @param {string | undefined} [lang]
* @example
* const t = useTranslations('en');
* const label = t('search.label'); // => 'Search'
*/
var useTranslations = await createTranslationSystem(user_config_default, await loadTranslations(), plugin_translations_default);
//#endregion
export { BuiltInDefaultLocale as n, user_config_default as r, useTranslations as t };
