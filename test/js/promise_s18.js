var thenable = {
    then: function(resolve) {
        resolve();
        console.log('State 5');
    }
};

var thenableWithError = {
    then: function(resolve) {
        console.log('State 3');
        resolve(thenable);
        console.log('State 4');
        throw new Error('ignored exception');
    }
};

function executor(resolve, reject) {
    console.log('State 1');
    resolve(thenableWithError);
    console.log('State 2');
}

new Promise(executor).then(() => {console.log('Done')}, () => {console.log('Error')});