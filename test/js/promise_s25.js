var resolve, reject;
var promise = new Promise(function(_resolve, _reject) {
    resolve = _resolve;
    reject = _reject;
});

var P = function(executor) {
    executor(resolve, reject);
    return promise;
};

Promise.resolve.call(P, promise)
.then(
function() {
    console.log('The promise should not be fulfilled.');
},
function(value) {
    if (!value) {
        console.log('The promise should be rejected with a value.');
        return;
    }

    if (value.constructor !== TypeError) {
        console.log('The promise should be rejected with a TypeError instance.');
        return;
    }

    console.log('Done');
});