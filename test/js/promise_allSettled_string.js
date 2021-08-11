function dump(v) {
    var fulfilled = v.filter(v=>v.status == 'fulfilled').map(v=>v.value).sort();
    var rejected = v.filter(v=>v.status == 'rejected').map(v=>v.reason).sort();
    return `F:${fulfilled}|R:${rejected}`;
}

Promise.allSettled("abc").then(
    (v) => {console.log(`resolved:${dump(v)}`)},
    (v) => {console.log(`rejected:${dump(v)}`)}
);
