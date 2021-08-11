var p0 = Promise.reject(1);
var p1 = Promise.reject(2);

Promise.any([p0, p1]).then(
    (v) => {console.log(`resolve:${v}`)},
    (v) => {console.log(`reject:${v}`)}
);
