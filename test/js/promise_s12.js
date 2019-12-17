var thenable = new Promise(function() {});

var p = new Promise(function(resolve) {
    resolve();
    throw thenable;
});

p.then(function() {
    console.log('Done');
});