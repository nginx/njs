var obj = {};
var p = Promise.resolve(obj);

p.then(undefined, undefined)
.then(function(arg) {
    if (arg !== obj) {
        console.log('Expected resolution object to be passed through, got ' + arg);
    }
})
.then(() => {console.log('Done')}, () => {console.log('Error')});