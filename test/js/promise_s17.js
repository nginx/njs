var obj = {};
var p = Promise.reject(obj);

p.then(undefined, undefined).then(function() {
    console.log('Should not be called -- promise was rejected.');
}, function(arg) {
    if (arg !== obj) {
        console.log('Expected resolution object to be passed through, got ' + arg);
    }
}).then(() => {console.log('Done')}, () => {console.log('Error')});
