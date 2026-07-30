from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session

from ..auth import require_web_session
from ..db import get_db
from ..models import Todo
from ..schemas import TodoIn, TodoOut, TodoPatch

router = APIRouter(tags=["todos"], dependencies=[Depends(require_web_session)])


@router.get("/api/todos", response_model=list[TodoOut])
def list_todos(db: Session = Depends(get_db)):
    return db.query(Todo).order_by(Todo.done.asc(), Todo.created_at.asc()).all()


@router.post("/api/todos", response_model=TodoOut)
def create_todo(body: TodoIn, db: Session = Depends(get_db)):
    text = body.text.strip()
    if not text:
        raise HTTPException(status_code=400, detail="text is required")
    todo = Todo(text=text[:256])
    db.add(todo)
    db.commit()
    db.refresh(todo)
    return todo


@router.patch("/api/todos/{todo_id}", response_model=TodoOut)
def update_todo(todo_id: int, body: TodoPatch, db: Session = Depends(get_db)):
    todo = db.get(Todo, todo_id)
    if todo is None:
        raise HTTPException(status_code=404, detail="todo not found")
    todo.done = body.done
    db.commit()
    db.refresh(todo)
    return todo


@router.delete("/api/todos/{todo_id}")
def delete_todo(todo_id: int, db: Session = Depends(get_db)):
    todo = db.get(Todo, todo_id)
    if todo is None:
        raise HTTPException(status_code=404, detail="todo not found")
    db.delete(todo)
    db.commit()
    return {"ok": True}


@router.post("/api/todos/clear_completed")
def clear_completed(db: Session = Depends(get_db)):
    deleted = db.query(Todo).filter(Todo.done.is_(True)).delete(synchronize_session=False)
    db.commit()
    return {"deleted": deleted}
