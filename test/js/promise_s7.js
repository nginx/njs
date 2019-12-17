var p1 = Promise.resolve({
    then: function(onFulfill, onReject) { onFulfill('fulfilled!'); }
});

console.log(p1 instanceof Promise);

p1.then(function(v) {
    console.log(v);
},
function(e) {
    console.log('ignored');
});