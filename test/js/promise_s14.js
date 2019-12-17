var isLoading = true;
var promise = new Promise((a, b) => {a()} );

promise.then(function(response) {
    throw new TypeError('oops');
})
.then(function(json) { })
.catch(function(error) { console.log(error); })
.finally(function() { console.log('Done') });