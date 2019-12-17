
Promise.resolve('Success').then(function(value) {
    console.log(value);
},
function(value) {
    console.log('ignored');
});