const search = document.querySelector("[data-doc-search]");
const cards = Array.from(document.querySelectorAll("[data-doc-path]"));
const groups = Array.from(document.querySelectorAll("[data-doc-group]"));
const count = document.querySelector("[data-doc-count]");
const empty = document.querySelector("[data-doc-empty]");
const clear = document.querySelector("[data-doc-clear]");

function normalize(value) {
    return value.toLocaleLowerCase().normalize("NFKD").replace(/[\u0300-\u036f]/g, "").trim();
}

function updateResults() {
    if (!(search instanceof HTMLInputElement)) {
        return;
    }
    const query = normalize(search.value);
    let visible = 0;
    for (const card of cards) {
        const searchable = normalize(`${card.textContent} ${card.dataset.docSearchable ?? ""} ${card.dataset.docPath ?? ""}`);
        card.hidden = query.length > 0 && !searchable.includes(query);
        if (!card.hidden) {
            ++visible;
        }
    }
    for (const group of groups) {
        if (!group.querySelector("[data-doc-path]")) {
            group.hidden = query.length > 0;
            continue;
        }
        group.hidden = !group.querySelector("[data-doc-path]:not([hidden])");
    }
    if (count instanceof HTMLElement) {
        count.textContent = `${visible} ${visible === 1 ? "document" : "documents"}`;
    }
    if (empty instanceof HTMLElement) {
        empty.hidden = visible !== 0;
    }
    if (clear instanceof HTMLButtonElement) {
        clear.hidden = query.length === 0;
    }
}

if (search instanceof HTMLInputElement) {
    search.addEventListener("input", updateResults);
    search.addEventListener("keydown", (event) => {
        if (event.key === "Escape" && search.value) {
            search.value = "";
            updateResults();
        }
    });
    document.addEventListener("keydown", (event) => {
        const target = event.target;
        const editing = target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement ||
            target instanceof HTMLSelectElement || target?.isContentEditable;
        if (event.key === "/" && !editing && !event.ctrlKey && !event.metaKey && !event.altKey) {
            event.preventDefault();
            search.focus();
        }
    });
}

if (clear instanceof HTMLButtonElement && search instanceof HTMLInputElement) {
    clear.addEventListener("click", () => {
        search.value = "";
        updateResults();
        search.focus();
    });
}
