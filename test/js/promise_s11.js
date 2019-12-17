var promise = new Promise((resolve, reject) => resolve('all'));

promise.then( function f1(result) {
    console.log('S: ' + result);
    return 'f1';
});

promise.then( function f2(result) {
    console.log('R: ' + result);
    return 'f2';
});

console.log('end')