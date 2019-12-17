var res = [];
function abc() {
    var promise = new Promise(function(resolve, reject) {
        res.push('One');
        resolve();
    });
    res.push('Two');
    promise.then(() => {res.push('Four'); return {num: 'Five'}})
    .then((obj) => {res.push(obj.num);  return {num: 'Six'}})
    .then((obj) => {res.push(obj.num);  return {num: 'Seven'}})
    .then((obj) => {res.push(obj.num);  return {num: 'Eight'}})
    .then((obj) => {res.push(obj.num)});
    res.push('Three');
}
abc();
console.log(res.join(','));
setTimeout(() => console.log(res.join(',')), 0);
