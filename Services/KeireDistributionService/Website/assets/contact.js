const endpoint = "https://khjduyjamzwumhducmou.supabase.co/functions/v1/website-contact";
const form = document.querySelector("[data-contact-form]");
const status = document.querySelector("[data-contact-status]");
const submit = document.querySelector("[data-contact-submit]");

function setStatus(state, message) {
    if (!(status instanceof HTMLElement)) {
        return;
    }
    status.dataset.state = state;
    status.textContent = message;
}

if (form instanceof HTMLFormElement && submit instanceof HTMLButtonElement) {
    form.addEventListener("submit", async (event) => {
        event.preventDefault();
        if (!form.reportValidity()) {
            return;
        }

        const fields = new FormData(form);
        const payload = {
            company: String(fields.get("company") ?? ""),
            name: String(fields.get("name") ?? ""),
            email: String(fields.get("email") ?? ""),
            category: String(fields.get("category") ?? ""),
            subject: String(fields.get("subject") ?? ""),
            message: String(fields.get("message") ?? ""),
        };

        submit.disabled = true;
        submit.setAttribute("aria-busy", "true");
        setStatus("pending", "Sending your message…");
        try {
            const response = await fetch(endpoint, {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(payload),
            });
            const result = await response.json().catch(() => ({}));
            const message = typeof result.message === "string" ? result.message : "Your message could not be sent.";
            if (!response.ok) {
                throw new Error(message);
            }
            form.reset();
            setStatus("success", message);
        } catch (error) {
            setStatus("error", error instanceof Error ? error.message : "Your message could not be sent. Please try again.");
        } finally {
            submit.disabled = false;
            submit.removeAttribute("aria-busy");
        }
    });
}
