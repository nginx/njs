Promise.reject(new Error('Oh my')).then(function(success) {
},
function(error) {
    console.log(error);
    throw error;
});