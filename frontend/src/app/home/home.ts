import { ChangeDetectionStrategy, Component } from '@angular/core';
import { RouterLink } from '@angular/router';
import { CommandBlock } from '../command-block/command-block';
import {
  CAPABILITIES,
  COMMANDS,
  NOT_GOALS,
  PREREQUISITES,
  README_URL,
  RESOURCES,
  ROADMAP,
  SNAPSHOT,
  SUPPORTED,
} from '../content';

@Component({
  selector: 'app-home',
  imports: [CommandBlock, RouterLink],
  templateUrl: './home.html',
  styleUrl: './home.scss',
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class HomePage {
  protected readonly snapshot = SNAPSHOT;
  protected readonly capabilities = CAPABILITIES;
  protected readonly supported = SUPPORTED;
  protected readonly notGoals = NOT_GOALS;
  protected readonly roadmap = ROADMAP;
  protected readonly commands = COMMANDS;
  protected readonly resources = RESOURCES;
  protected readonly prerequisites = PREREQUISITES;
  protected readonly readmeUrl = README_URL;
}
