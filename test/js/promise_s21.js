var value = {};
var resolve;
var poisonedThen = Object.defineProperty({}, 'then', {
    get: function() {
        throw value;
    }
});

var p1 = new Promise(function(_resolve) {
    resolve = _resolve;
});

var p2;

p2 = p1.then(function() {
    return poisonedThen;
});

p2.then(function(x) {
    console.log('The promise should not be fulfilled.');
}, function(x) {
    if (x !== value) {
        console.log('The promise should be rejected with the thrown exception.');
        return;
    }

    console.log('Done');
});

resolve();