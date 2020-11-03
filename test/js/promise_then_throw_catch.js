Promise.resolve()
.then(() => {nonExsisting()})
.catch(() => {console.log("Done")});