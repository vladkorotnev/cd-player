import { mount } from 'svelte';
import App from './App.svelte';
import './style.css';

const target = document.getElementById('app');
if (!target) throw new Error('Missing application mount point');
mount(App, { target });
