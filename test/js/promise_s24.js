var checkPoint = '';

Promise.reject.call(function(executor) {
    checkPoint += 'a';
    executor();

    checkPoint += 'b';
    executor(function() {}, function() {});

    checkPoint += 'c';
}, {});

console.log(checkPoint == 'abc');