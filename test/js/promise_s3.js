
var promise = new Promise(function(resolve, reject) {
    console.log('One');
    reject(new Error('Blah'));
});

console.log('Two');

promise.then((response) => console.log(`Fulfilled: ${response}`), (error) => console.log(`Rejected: ${error}`));

console.log('Three');