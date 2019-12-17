var executorFunction;

function NotPromise(executor) {
  executorFunction = executor;
  executor(function() {console.log('S')}, function() {console.log('R')});
}

Promise.resolve.call(NotPromise);

console.log(Object.isExtensible(executorFunction));