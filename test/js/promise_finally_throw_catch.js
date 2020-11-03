Promise.resolve()
.finally(() => {nonExsistingInFinally()})
.catch(() => {console.log("Done")});