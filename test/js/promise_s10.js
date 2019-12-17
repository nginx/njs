var promise = new Promise(function(resolve, reject) {
    console.log('One');
    reject('Oh no');
});

console.log('Two');

promise.then(() => {console.log('Three')})
.catch((v) => {console.log(v)});

console.log('Three');