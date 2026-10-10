// Board positions use displayed grid coordinates, including the flipped timeline order.
export function nearestBoard(boards, position, spacing) {
    let nearest = null;
    let distance = Infinity;
    for (const board of boards) {
        const candidate = ((board.x - position.x) * spacing.x) ** 2
            + ((board.y - position.y) * spacing.y) ** 2;
        if (candidate < distance) {
            nearest = board;
            distance = candidate;
        }
    }
    return nearest;
}

export function directionalTarget(boards, position, direction) {
    const origin = { x: Math.round(position.x), y: Math.round(position.y) };
    const horizontal = direction.x !== 0;
    const axis = horizontal ? 'x' : 'y';
    const crossAxis = horizontal ? 'y' : 'x';
    const sign = direction[axis];
    let target = { x: origin.x + direction.x, y: origin.y + direction.y };
    let distance = Infinity;
    for (const board of boards) {
        const ahead = (board[axis] - origin[axis]) * sign;
        if (board[crossAxis] === origin[crossAxis] && ahead > 0 && ahead < distance) {
            target = board;
            distance = ahead;
        }
    }
    return target;
}
