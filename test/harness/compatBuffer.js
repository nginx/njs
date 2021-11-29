function has_buffer() {
    return (typeof Buffer === 'function') && Buffer.from;
}
