Promise.resolve()
.then(() => {})
.catch(() => {})
.then(() => {nonExsisting()})
.catch(() => {console.log("Done")});