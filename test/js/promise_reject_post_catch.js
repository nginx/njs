var p = Promise.reject();
setImmediate(() => {p.catch(() => {})});