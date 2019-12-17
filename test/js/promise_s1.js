var promise = new Promise(function(resolve, reject) {
    console.log('One');

    reject(123);
}).catch((v) => {console.log(v)});

console.log('Two');

promise.then(() => {console.log('Four'); return {num: 'Five'}})
.then((obj) => {console.log(obj.num);  return {num: 'Six'}})
.then((obj) => {console.log(obj.num);  return {num: 'Seven'}})
.then((obj) => {console.log(obj.num);  return {num: 'Eight'}})
.then((obj) => {console.log(obj.num)});

console.log('Three');