Promise.resolve()
.then(() => {nonExsisting()})
.catch(() => {nonExsistingInCatch()});