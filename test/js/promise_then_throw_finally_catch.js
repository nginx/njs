Promise.resolve()
.then(() => {nonExsisting()})
.finally(() => {})
.catch(() => {console.log("Done")});