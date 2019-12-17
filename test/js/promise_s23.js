var returnValue = null;
var resolve;

var promise = new Promise(function(_resolve) {
    resolve = _resolve;
});

promise.then(
function() {
    console.log('The promise should not be fulfilled.');
},
function(reason) {
    if (!reason) {
        console.log('The promise should be rejected with a value.');
        return;
    }

    if (reason.constructor !== TypeError) {
        console.log('The promise should be rejected with a TypeError instance.');
        return;
    }

    console.log('Done');
});

returnValue = resolve(promise);

console.log(returnValue == undefined)