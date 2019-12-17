var returnValue = null;
var value = {};
var resolve;

var poisonedThen = Object.defineProperty({}, 'then', {
    get: function() {
        console.log('Throw!');
        throw value;
    }
});

var promise = new Promise(function(_resolve) {
    resolve = _resolve;
});

promise.then(
function() {
    console.log('Resolve!');
    console.log('The promise should not be fulfilled.');
},
function(val) {
    console.log('Reject!');
    if (val !== value) {
        console.log('The promise should be fulfilled with the provided value.');
        return;
    }

    console.log('Done');
});

returnValue = resolve(poisonedThen);

console.log(returnValue);