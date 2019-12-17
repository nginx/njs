var p = Promise.resolve([1,2,3]);
p.then(function(v) {
    console.log(v[0]);
});