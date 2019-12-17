var returnValue = null;
var value = {};
var poisonedThen = Object.defineProperty({}, 'then', {
    get: function() {
        throw value;
    }
});
var promise = new Promise(function(resolve) {
    returnValue = resolve(poisonedThen);
});

console.log(returnValue == undefined);

promise.then(function() {
    console.log('The promise should not be fulfilled.');
}, function(val) {
    if (val !== value) {
        console.log('The promise should be fulfilled with the provided value.');
        return;
    }

    console.log('Done');
});