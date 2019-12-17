var thenable = {
    then: function(resolve) {
        console.log('Ok')
        resolve();
    }
};

function executor(resolve, reject) {
    resolve(thenable);
    throw new Error('ignored');
}

new Promise(executor).then(() => {});