/** Stop unused preconnections without interrupting requests already handed to the application. */
export function createQaServerDrain(server, secure) {
    const unstarted = new Set();
    let closing = false;
    let shutdown;
    server.on(secure ? "secureConnection" : "connection", (socket) => {
        if (closing) {
            socket.destroy();
            return;
        }
        unstarted.add(socket);
        socket.once("close", () => unstarted.delete(socket));
    });
    server.prependListener("request", (request) => {
        unstarted.delete(request.socket);
    });
    return () => {
        if (!shutdown) {
            closing = true;
            shutdown = new Promise((resolve, reject) => {
                server.close((error) => error ? reject(error) : resolve());
            });
            for (const socket of unstarted) {
                socket.destroy();
            }
            unstarted.clear();
        }
        return shutdown;
    };
}
