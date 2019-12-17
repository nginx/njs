var thenable = Promise.resolve();
var p = new Promise(function(a,b) {
    throw thenable;
});

p.then(function() {
    console.log('The promise should not be fulfilled.');
})
.then(
    function() {
        console.log('The promise should not be fulfilled.');
    },
    function(x) {
        if (x !== thenable) {
            console.log('The promise should be rejected with the resolution value.');
            return;
        }

        console.log('Done');
    }
);