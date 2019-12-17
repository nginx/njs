var returnValue = null;
var value = {};
var resolve;

var thenable = new Promise(function(resolve) {
    resolve();
});

var promise = new Promise(function(_resolve) {
    resolve = _resolve;
});

thenable.then = function(resolve) {
    resolve(value);
};

promise.then(
function(val) {
    if (val !== value) {
        console.log('The promise should be fulfilled with the provided value.');
        return;
    }

    console.log('Done');
},
function() {
    console.log('The promise should not be rejected.');
});

returnValue = resolve(thenable);

console.log(returnValue == undefined);